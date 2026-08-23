#include "rede.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <stdio.h>

// O DNS de captura responde QUALQUER nome com o IP da maquina. Duas
// consequencias, as duas boas:
//   - o celular detecta "portal cativo" ao entrar na rede e oferece
//     abrir a pagina sozinho, em vez de reclamar que nao ha internet e
//     pular para os dados moveis;
//   - digitar qualquer coisa na barra de endereco cai no painel.
static DNSServer dns;
static bool dnsNoAr = false;

const char* redeNomeLocal() { return WIFI_NOME_LOCAL; }

const char* redeIpAcesso() {
  static char ip[16];
  snprintf(ip, sizeof(ip), "%u.%u.%u.%u",
           (unsigned)WIFI_AP_IP[0], (unsigned)WIFI_AP_IP[1],
           (unsigned)WIFI_AP_IP[2], (unsigned)WIFI_AP_IP[3]);
  return ip;
}

// ---------------------------------------------------------------------
void redeIniciar() {
  // WIFI_AP e nao WIFI_AP_STA: um radio so, um canal so, sem dividir
  // tempo com rede nenhuma. E o que mantem o heartbeat do jog previsivel.
  WiFi.persistent(false);   // nada de credencial guardada pela biblioteca
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);     // economia de energia atrasa o heartbeat
  WiFi.setHostname(WIFI_NOME_LOCAL);

  // IP fixo declarado pelo projeto, e nao herdado do padrao da
  // biblioteca: o endereco do painel nao pode mudar entre versoes do
  // core do ESP32.
  const IPAddress ip(WIFI_AP_IP[0], WIFI_AP_IP[1], WIFI_AP_IP[2], WIFI_AP_IP[3]);
  const IPAddress mascara(255, 255, 255, 0);
  WiFi.softAPConfig(ip, ip, mascara);

  if (!WiFi.softAP(WIFI_AP_SSID, WIFI_AP_SENHA)) {
    Serial.println("[REDE] FALHA ao subir o ponto de acesso!");
    return;
  }

  if (MDNS.begin(WIFI_NOME_LOCAL)) MDNS.addService("http", "tcp", 80);

  dnsNoAr = dns.start(53, "*", ip);

  Serial.println("[REDE] Wi-Fi proprio ativo.");
  Serial.print  ("[REDE]   SSID : "); Serial.println(WIFI_AP_SSID);
  Serial.print  ("[REDE]   Senha: "); Serial.println(WIFI_AP_SENHA);
  Serial.print  ("[REDE]   Painel: http://"); Serial.println(redeIpAcesso());
  Serial.print  ("[REDE]           http://"); Serial.print(WIFI_NOME_LOCAL);
  Serial.println(".local");
  if (dnsNoAr) Serial.println("[REDE]   Qualquer endereco cai no painel.");
}

// ---------------------------------------------------------------------
void redeAtender() {
  // Barato: so olha se chegou pacote UDP na porta 53.
  if (dnsNoAr) dns.processNextRequest();
}
