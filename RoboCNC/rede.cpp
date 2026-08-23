#include "rede.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <string.h>
#include <stdio.h>

volatile bool redePedidoReconectar = false;

static EstadoEstacao estado     = EST_DESLIGADA;
static uint32_t      sequencia  = 0;
static uint32_t      tentativaEm = 0;
static uint32_t      proximaTentativa = 0;
static uint8_t       falhasSeguidas = 0;
static char          ipEstacao[16] = "";

static RedeVizinha vizinhas[MAX_REDES_VIZINHAS];
static uint8_t     nVizinhas = 0;
static bool        varrendo  = false;

// ---------------------------------------------------------------------
EstadoEstacao redeEstado()    { return estado; }
const char*   redeIpEstacao() { return ipEstacao; }
const char*   redeNomeLocal() { return WIFI_NOME_LOCAL; }
uint32_t      redeSequencia() { return sequencia; }
bool          redeVarrendo()  { return varrendo; }
uint8_t            redeVizinhasN() { return nVizinhas; }
const RedeVizinha* redeVizinhas()  { return vizinhas; }

int32_t redeSinal() {
  return (estado == EST_CONECTADA) ? (int32_t)WiFi.RSSI() : 0;
}

const char* redeIpAcesso() {
  static char ip[16];
  snprintf(ip, sizeof(ip), "%u.%u.%u.%u",
           (unsigned)WIFI_AP_IP[0], (unsigned)WIFI_AP_IP[1],
           (unsigned)WIFI_AP_IP[2], (unsigned)WIFI_AP_IP[3]);
  return ip;
}

const char* redeEstadoTexto() {
  switch (estado) {
    case EST_DESLIGADA:  return "DESLIGADA";
    case EST_CONECTANDO: return "CONECTANDO";
    case EST_CONECTADA:  return "CONECTADA";
    case EST_SEM_REDE:   return "SEM_REDE";
    case EST_SENHA:      return "SENHA";
    default:             return "FALHOU";
  }
}

static void mudarPara(EstadoEstacao novo) {
  if (estado == novo) return;
  estado = novo;
  sequencia++;
}

// ---------------------------------------------------------------------
static void subirPontoDeAcesso() {
  // IP fixo, declarado aqui e nao herdado do padrao da biblioteca: o
  // endereco do painel nao pode mudar entre versoes do core do ESP32.
  const IPAddress ip(WIFI_AP_IP[0], WIFI_AP_IP[1], WIFI_AP_IP[2], WIFI_AP_IP[3]);
  const IPAddress mascara(255, 255, 255, 0);
  WiFi.softAPConfig(ip, ip, mascara);

  if (!WiFi.softAP(WIFI_AP_SSID, WIFI_AP_SENHA)) {
    Serial.println("[REDE] FALHA ao subir o ponto de acesso!");
    return;
  }
  Serial.println("[REDE] Ponto de acesso proprio ativo (sempre ligado).");
  Serial.print  ("[REDE]   SSID : "); Serial.println(WIFI_AP_SSID);
  Serial.print  ("[REDE]   Senha: "); Serial.println(WIFI_AP_SENHA);
  Serial.print  ("[REDE]   Painel: http://"); Serial.println(redeIpAcesso());
}

static void tentarEntrarNaRede() {
  if (wifiSsid[0] == '\0') { mudarPara(EST_DESLIGADA); return; }
  Serial.print("[REDE] Entrando na rede \"");
  Serial.print(wifiSsid);
  Serial.println("\"...");
  WiFi.begin(wifiSsid, wifiSenha);
  tentativaEm = millis();
  mudarPara(EST_CONECTANDO);
}

// ---------------------------------------------------------------------
void redeIniciar() {
  // AP_STA e o modo dos dois ao mesmo tempo: o ponto de acesso proprio
  // nunca sai do ar, mesmo com a maquina dentro da rede da oficina.
  WiFi.persistent(false);       // as credenciais sao nossas, no NVS do projeto
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);         // economia de energia atrasa o heartbeat do jog
  WiFi.setHostname(WIFI_NOME_LOCAL);
  WiFi.setAutoReconnect(true);

  subirPontoDeAcesso();

  // O nome resolve nas DUAS interfaces: http://robo2dof.local funciona
  // tanto pelo Wi-Fi da maquina quanto pela rede da oficina, e nao
  // depende de qual IP o roteador entregou.
  if (MDNS.begin(WIFI_NOME_LOCAL)) {
    MDNS.addService("http", "tcp", 80);
    Serial.print("[REDE] Nome na rede: http://");
    Serial.print(WIFI_NOME_LOCAL);
    Serial.println(".local");
  } else {
    Serial.println("[REDE] mDNS nao subiu; use o IP.");
  }

  if (wifiSsid[0]) tentarEntrarNaRede();
  else Serial.println("[REDE] Nenhuma rede configurada. So o ponto de acesso proprio.");
}

// ---------------------------------------------------------------------
static void colherVarredura(int n) {
  nVizinhas = 0;
  for (int i = 0; i < n && nVizinhas < MAX_REDES_VIZINHAS; i++) {
    RedeVizinha& r = vizinhas[nVizinhas];
    const char* s = WiFi.SSID(i);
    if (!s || !s[0]) continue;                 // rede oculta: nada a mostrar
    // Duplicata: o mesmo SSID aparece uma vez por canal e por banda.
    bool repetida = false;
    for (uint8_t k = 0; k < nVizinhas; k++)
      if (!strcmp(vizinhas[k].ssid, s)) { repetida = true; break; }
    if (repetida) continue;

    strncpy(r.ssid, s, sizeof(r.ssid) - 1);
    r.ssid[sizeof(r.ssid) - 1] = '\0';
    r.rssi   = WiFi.RSSI(i);
    r.canal  = (uint8_t)WiFi.channel(i);
    r.aberta = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
    nVizinhas++;
  }
  // Mais forte primeiro: e a ordem em que o operador quer escolher.
  for (uint8_t i = 1; i < nVizinhas; i++) {
    RedeVizinha t = vizinhas[i];
    int8_t k = (int8_t)i - 1;
    while (k >= 0 && vizinhas[k].rssi < t.rssi) { vizinhas[k + 1] = vizinhas[k]; k--; }
    vizinhas[k + 1] = t;
  }
  WiFi.scanDelete();
  sequencia++;
}

bool redeVarrerIniciar() {
  if (varrendo) return false;
  nVizinhas = 0;
  // O 'true' e o que importa: varredura assincrona. A sincrona bloqueia
  // esta tarefa por segundos e o supervisor corta o movimento por falta
  // de heartbeat.
  WiFi.scanNetworks(true);
  varrendo = true;
  sequencia++;
  return true;
}

// ---------------------------------------------------------------------
void redeAtender() {
  if (varrendo) {
    const int n = WiFi.scanComplete();
    if (n >= 0)                  { varrendo = false; colherVarredura(n); }
    else if (n == WIFI_SCAN_FAILED) { varrendo = false; nVizinhas = 0; sequencia++; }
  }

  if (redePedidoReconectar) {
    redePedidoReconectar = false;
    WiFi.disconnect(false, true);
    falhasSeguidas = 0;
    proximaTentativa = 0;
    ipEstacao[0] = '\0';
    if (wifiSsid[0]) tentarEntrarNaRede();
    else { mudarPara(EST_DESLIGADA); Serial.println("[REDE] Rede esquecida."); }
    return;
  }

  switch (estado) {
    case EST_CONECTANDO: {
      const int st = WiFi.status();
      if (st == WL_CONNECTED) {
        strncpy(ipEstacao, WiFi.localIP(), sizeof(ipEstacao) - 1);
        ipEstacao[sizeof(ipEstacao) - 1] = '\0';
        falhasSeguidas = 0;
        mudarPara(EST_CONECTADA);
        Serial.print("[REDE] Na rede \""); Serial.print(wifiSsid);
        Serial.print("\", IP "); Serial.println(ipEstacao);
        Serial.print("[REDE] Painel tambem em http://");
        Serial.print(WIFI_NOME_LOCAL); Serial.println(".local");
      } else if (st == WL_NO_SSID_AVAIL) {
        mudarPara(EST_SEM_REDE);
      } else if (st == WL_CONNECT_FAILED) {
        mudarPara(EST_SENHA);
      } else if (millis() - tentativaEm > WIFI_STA_TIMEOUT_MS) {
        mudarPara(EST_FALHOU);
      }
      if (estado != EST_CONECTANDO && estado != EST_CONECTADA) {
        // Recuo progressivo. Insistir de 1 em 1 segundo com a senha
        // errada so atrapalha o proprio ponto de acesso: cada tentativa
        // tira o radio do canal do AP por um instante.
        if (falhasSeguidas < 250) falhasSeguidas++;
        uint32_t espera = (uint32_t)falhasSeguidas * WIFI_RETENTATIVA_MS;
        if (espera > WIFI_RETENTATIVA_MAX_MS) espera = WIFI_RETENTATIVA_MAX_MS;
        proximaTentativa = millis() + espera;
        Serial.print("[REDE] Nao entrou ("); Serial.print(redeEstadoTexto());
        Serial.print("); nova tentativa em "); Serial.print(espera / 1000);
        Serial.println(" s. O ponto de acesso proprio continua no ar.");
      }
      break;
    }

    case EST_CONECTADA:
      if (WiFi.status() != WL_CONNECTED) {
        ipEstacao[0] = '\0';
        mudarPara(EST_FALHOU);
        proximaTentativa = millis() + WIFI_RETENTATIVA_MS;
        Serial.println("[REDE] Saiu da rede da oficina. O painel continua em 192.168.4.1.");
      }
      break;

    case EST_SEM_REDE:
    case EST_SENHA:
    case EST_FALHOU:
      // Senha recusada nao se resolve sozinha, mas o roteador pode ter
      // sido religado: continua tentando, devagar.
      if (wifiSsid[0] && proximaTentativa && millis() >= proximaTentativa) {
        proximaTentativa = 0;
        tentarEntrarNaRede();
      }
      break;

    default:
      break;
  }
}
