#include "armazenamento.h"
#include "estado.h"
#include "cinematica.h"
#include "trajetoria.h"
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#if CARTAO_INSTALADO
  #include <SPI.h>
  #include <SD.h>
  #include <FS.h>
  static SPIClass spiCartao(HSPI);
#endif

// ---------------------------------------------------------------------
// Estado publicado
// ---------------------------------------------------------------------
static volatile ArmEstado estado = ARM_DESLIGADO;
static char       mensagem[80]   = "cartao nao iniciado";
static volatile uint32_t sequencia = 0;
static uint64_t   bytesTotais = 0;
static uint64_t   bytesLivres = 0;

static ArmEntrada lista[MAX_ARQ_LISTA];
static uint8_t    listaN    = 0;
static ArmTipo    listaTipo = TIPO_PROG;

// Area de troca do programa
static Ponto   stagingPontos[MAX_PONTOS];
static uint8_t stagingN     = 0;
static float   stagingElo1  = 0.0f;
static float   stagingElo2  = 0.0f;

// Pedido em andamento
struct Pedido { ArmTarefa tarefa; char nome[MAX_NOME_ARQ + 8]; };
static QueueHandle_t filaPedidos = nullptr;

// Registro de eventos
struct LinhaLog { uint32_t ms; char txt[72]; };
static QueueHandle_t filaLog = nullptr;
static uint32_t      sessao  = 0;
static char          arquivoLog[24] = "";

// ---------------------------------------------------------------------
ArmEstado   armEstado()      { return estado; }
bool        armOcupado()     { return estado == ARM_OCUPADO; }
const char* armMensagem()    { return mensagem; }
uint32_t    armSequencia()   { return sequencia; }
uint64_t    armBytesTotais() { return bytesTotais; }
uint64_t    armBytesLivres() { return bytesLivres; }

uint8_t           armListaN()    { return listaN; }
const ArmEntrada* armLista()     { return lista; }
ArmTipo           armListaTipo() { return listaTipo; }

uint8_t      armStagingN()      { return stagingN; }
const Ponto* armStagingPontos() { return stagingPontos; }
float        armStagingElo1()   { return stagingElo1; }
float        armStagingElo2()   { return stagingElo2; }

void armStagingDefinir(const Ponto* pontos, uint8_t n) {
  if (n > MAX_PONTOS) n = MAX_PONTOS;
  memcpy(stagingPontos, pontos, (size_t)n * sizeof(Ponto));
  stagingN    = n;
  stagingElo1 = elo1Mm;
  stagingElo2 = elo2Mm;
}

static void definirResultado(ArmEstado novo, const char* fmt, ...) {
  va_list a; va_start(a, fmt);
  vsnprintf(mensagem, sizeof(mensagem), fmt, a);
  va_end(a);
  estado = novo;
  sequencia++;
}

// ---------------------------------------------------------------------
// Nomes e caminhos
// ---------------------------------------------------------------------
ArmTipo armTipoDe(const char* t) {
  if (!t) return TIPO_INVALIDO;
  if (!strcmp(t, "prog")) return TIPO_PROG;
  if (!strcmp(t, "traj")) return TIPO_TRAJ;
  if (!strcmp(t, "cfg"))  return TIPO_CFG;
  return TIPO_INVALIDO;
}

static const char* pastaDe(ArmTipo t) {
  switch (t) {
    case TIPO_PROG: return "/prog";
    case TIPO_TRAJ: return "/traj";
    case TIPO_CFG:  return "/cfg";
    default:        return "/";
  }
}
static const char* extDe(ArmTipo t) {
  switch (t) {
    case TIPO_PROG: return ".prg";
    case TIPO_TRAJ: return ".trj";
    case TIPO_CFG:  return ".cfg";
    default:        return ".dat";
  }
}

// So [A-Za-z0-9 _-]. Sem ponto, sem barra, sem "..": o nome vem de uma
// requisicao HTTP e nao pode virar caminho.
bool armNomeValido(const char* nome) {
  if (!nome) return false;
  const size_t n = strlen(nome);
  if (n == 0 || n > MAX_NOME_ARQ) return false;
  for (size_t i = 0; i < n; i++) {
    const char c = nome[i];
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '_' || c == '-' || c == ' ';
    if (!ok) return false;
  }
  return nome[0] != ' ' && nome[n - 1] != ' ';
}

static void caminhoDe(char* destino, size_t tam, ArmTipo t, const char* nome) {
  snprintf(destino, tam, "%s/%s%s", pastaDe(t), nome, extDe(t));
}

// ---------------------------------------------------------------------
void logEvento(const char* fmt, ...) {
  if (!filaLog) return;
  LinhaLog l;
  l.ms = millis();
  va_list a; va_start(a, fmt);
  vsnprintf(l.txt, sizeof(l.txt), fmt, a);
  va_end(a);
  // Timeout zero: o log jamais segura o laco de controle.
  xQueueSend(filaLog, &l, 0);
}

bool armSolicitar(ArmTarefa t, const char* nome) {
  if (!filaPedidos) return false;
  if (estado == ARM_DESLIGADO) return false;
  if (estado == ARM_OCUPADO)   return false;
  // Sem cartao so faz sentido tentar montar. Recusar aqui faz o handler
  // HTTP devolver erro na hora e o core 1 avisar na tira de mensagem, em
  // vez de o operador so descobrir olhando o painel do cartao.
  if (estado == ARM_SEM_CARTAO && t != TAR_MONTAR) return false;
  Pedido p;
  p.tarefa = t;
  strncpy(p.nome, nome ? nome : "", sizeof(p.nome) - 1);
  p.nome[sizeof(p.nome) - 1] = '\0';
  if (xQueueSend(filaPedidos, &p, 0) != pdTRUE) return false;
  estado = ARM_OCUPADO;
  return true;
}

// =====================================================================
//  Daqui para baixo, tudo roda DENTRO da tarefa do core 0.
// =====================================================================
#if CARTAO_INSTALADO

static bool montado = false;

static void atualizarEspaco() {
  bytesTotais = SD.totalBytes();
  bytesLivres = bytesTotais - SD.usedBytes();
}

static void garantirPastas() {
  const char* pastas[] = { "/prog", "/traj", "/cfg", "/log" };
  for (uint8_t i = 0; i < 4; i++) {
    if (!SD.exists(pastas[i])) SD.mkdir(pastas[i]);
  }
}

// Um arquivo de log por partida. O ESP32 nao tem relogio de tempo real,
// entao a ordem vem de um contador guardado no NVS - sem ele, todos os
// arquivos teriam o mesmo nome e a cada boot o anterior sumiria.
static void abrirLog() {
  snprintf(arquivoLog, sizeof(arquivoLog), "/log/s%04u.csv",
           (unsigned)(sessao % 10000));
  if (!SD.exists(arquivoLog)) {
    File f = SD.open(arquivoLog, FILE_WRITE);
    if (f) {
      f.println("ms_desde_boot;evento");
      f.close();
    }
  }
  // Higiene: apaga o log mais antigo quando passar do teto.
  File dir = SD.open("/log");
  if (!dir) return;
  uint16_t n = 0;
  uint32_t menor = 0xFFFFFFFF;
  char maisAntigo[24] = "";
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    const char* nm = f.name();
    const char* base = strrchr(nm, '/');
    base = base ? base + 1 : nm;
    if (base[0] == 's') {
      n++;
      const uint32_t idx = (uint32_t)atoi(base + 1);
      if (idx < menor) { menor = idx; snprintf(maisAntigo, sizeof(maisAntigo), "/log/%s", base); }
    }
    f.close();
  }
  dir.close();
  if (n > MAX_LOG_SESSOES && maisAntigo[0]) SD.remove(maisAntigo);
}

static void listar(ArmTipo tipo);

static bool montar() {
  if (montado) return true;

  spiCartao.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  if (!SD.begin(PIN_SD_CS, spiCartao, SD_FREQ_HZ)) {
    spiCartao.end();
    montado = false;
    return false;
  }
  if (SD.cardType() == CARD_NONE) {
    SD.end();
    spiCartao.end();
    montado = false;
    return false;
  }
  montado = true;
  garantirPastas();
  atualizarEspaco();
  abrirLog();
  listar(TIPO_PROG);   // a aba Arquivos ja abre com a biblioteca na tela
  return true;
}

static void desmontar() {
  if (!montado) return;
  SD.end();
  spiCartao.end();
  montado = false;
}

// ---------------------------------------------------------------------
// PROGRAMA - texto, em GRAUS.
//
// Guardar passos amarraria o arquivo a engrenagem eletronica em uso:
// trocar a resolucao mudaria de lugar todos os pontos gravados. Em
// graus o arquivo continua valendo, e ainda da para escrever um
// programa no PC com um editor de texto.
// ---------------------------------------------------------------------
static bool salvarPrograma(const char* nome) {
  char caminho[64];
  caminhoDe(caminho, sizeof(caminho), TIPO_PROG, nome);

  File f = SD.open(caminho, FILE_WRITE);
  if (!f) return false;

  f.println("ROBO2DOF-PROG 1");
  f.printf("nome=%s\n", nome);
  f.printf("elos=%.3f,%.3f\n", stagingElo1, stagingElo2);
  f.printf("pontos=%u\n", (unsigned)stagingN);
  f.println("# t1(graus) t2(graus) solda_ate_o_proximo");
  for (uint8_t i = 0; i < stagingN; i++) {
    f.printf("%.4f %.4f %u\n",
             passosParaGraus(J1, stagingPontos[i].p1),
             passosParaGraus(J2, stagingPontos[i].p2),
             (unsigned)stagingPontos[i].soldaAteProximo);
  }
  f.close();
  return true;
}

static bool carregarPrograma(const char* nome, char* erro, size_t tamErro) {
  char caminho[64];
  caminhoDe(caminho, sizeof(caminho), TIPO_PROG, nome);

  File f = SD.open(caminho, FILE_READ);
  if (!f) { snprintf(erro, tamErro, "arquivo nao encontrado"); return false; }

  char linha[96];
  uint8_t n = 0;
  float e1 = 0, e2 = 0;
  bool cabecalhoOk = false;

  while (f.available()) {
    size_t k = 0;
    while (f.available() && k < sizeof(linha) - 1) {
      const char c = (char)f.read();
      if (c == '\n') break;
      if (c != '\r') linha[k++] = c;
    }
    linha[k] = '\0';
    if (k == 0 || linha[0] == '#') continue;

    if (!cabecalhoOk) {
      if (strncmp(linha, "ROBO2DOF-PROG", 12) != 0) {
        f.close();
        snprintf(erro, tamErro, "nao e um programa do Robo2dof");
        return false;
      }
      cabecalhoOk = true;
      continue;
    }
    if (!strncmp(linha, "elos=", 5)) { sscanf(linha + 5, "%f,%f", &e1, &e2); continue; }
    if (!strncmp(linha, "nome=", 5) || !strncmp(linha, "pontos=", 7)) continue;

    float t1 = 0, t2 = 0;
    unsigned s = 0;
    if (sscanf(linha, "%f %f %u", &t1, &t2, &s) != 3) continue;
    if (n >= MAX_PONTOS) {
      f.close();
      snprintf(erro, tamErro, "programa tem mais de %u pontos", (unsigned)MAX_PONTOS);
      return false;
    }
    stagingPontos[n].p1 = (int32_t)grausParaPassos(J1, t1);
    stagingPontos[n].p2 = (int32_t)grausParaPassos(J2, t2);
    stagingPontos[n].soldaAteProximo = (s != 0) ? 1 : 0;
    n++;
  }
  f.close();

  if (!cabecalhoOk) { snprintf(erro, tamErro, "arquivo vazio ou ilegivel"); return false; }
  if (n < 2)        { snprintf(erro, tamErro, "programa com menos de 2 pontos"); return false; }

  stagingN    = n;
  stagingElo1 = e1;
  stagingElo2 = e2;
  return true;
}

// ---------------------------------------------------------------------
// TRAJETORIA - binario. 1500 waypoints em texto seriam ~40 kB e uma
// montanha de sscanf; em registro fixo sao 18 kB e uma leitura direta.
// O buffer vivo e emprestado pelo core 1 (ver trajEmprestar), entao nao
// existe copia intermediaria de 18 kB na RAM.
// ---------------------------------------------------------------------
static const uint32_t TRJ_MAGICO = 0x314A5254;   // "TRJ1"

static bool salvarTrajetoria(const char* nome) {
  char caminho[64];
  caminhoDe(caminho, sizeof(caminho), TIPO_TRAJ, nome);

  const uint16_t n = trajPontos();
  if (n < 2) return false;

  File f = SD.open(caminho, FILE_WRITE);
  if (!f) return false;

  f.write((const uint8_t*)&TRJ_MAGICO, sizeof(TRJ_MAGICO));
  f.write((const uint8_t*)&n, sizeof(n));
  const float ppg1 = J1.passosPorGrau, ppg2 = J2.passosPorGrau;
  f.write((const uint8_t*)&ppg1, sizeof(ppg1));
  f.write((const uint8_t*)&ppg2, sizeof(ppg2));
  f.write((const uint8_t*)trajBuffer(), (size_t)n * sizeof(Waypoint));
  f.close();
  return true;
}

static bool carregarTrajetoria(const char* nome, char* erro, size_t tamErro) {
  char caminho[64];
  caminhoDe(caminho, sizeof(caminho), TIPO_TRAJ, nome);

  File f = SD.open(caminho, FILE_READ);
  if (!f) { snprintf(erro, tamErro, "arquivo nao encontrado"); return false; }

  uint32_t magico = 0;
  uint16_t n = 0;
  float ppg1 = 0, ppg2 = 0;
  // File::read() devolve int (negativo em erro): compare como int.
  if ((int)f.read((uint8_t*)&magico, sizeof(magico)) != (int)sizeof(magico) ||
      (int)f.read((uint8_t*)&n, sizeof(n))           != (int)sizeof(n)      ||
      (int)f.read((uint8_t*)&ppg1, sizeof(ppg1))     != (int)sizeof(ppg1)   ||
      (int)f.read((uint8_t*)&ppg2, sizeof(ppg2))     != (int)sizeof(ppg2)) {
    f.close(); snprintf(erro, tamErro, "cabecalho incompleto"); return false;
  }
  // Valida o cabecalho ANTES de escrever no buffer vivo: o buffer esta
  // emprestado, e um arquivo torto nao pode virar trajetoria pela metade.
  if (magico != TRJ_MAGICO) {
    f.close(); snprintf(erro, tamErro, "nao e uma trajetoria do Robo2dof"); return false;
  }
  if (n < 2 || n > MAX_WAYPOINTS) {
    f.close(); snprintf(erro, tamErro, "trajetoria com %u pontos", (unsigned)n); return false;
  }

  // O buffer vivo esta emprestado pelo core 1 (CMD_ARQ_CARREGAR_TRAJ),
  // entao a leitura vai direto para ele: sem copia de 18 kB na RAM.
  Waypoint* destino = trajBufferGravavel();
  const int esperado = (int)((size_t)n * sizeof(Waypoint));
  const int lidos = destino ? (int)f.read((uint8_t*)destino, (size_t)esperado) : 0;
  f.close();
  if (lidos != esperado) {
    trajDefinirN(0);
    snprintf(erro, tamErro, "leitura incompleta: arquivo truncado");
    return false;
  }
  trajDefinirN(n);

  // Passos gravados com outra resolucao apontam para outro angulo.
  if (ppg1 > 0.0f && J1.passosPorGrau > 0.0f &&
      (ppg1 / J1.passosPorGrau < 0.999f || ppg1 / J1.passosPorGrau > 1.001f)) {
    snprintf(erro, tamErro, "gravada com outra resolucao: confira antes de reproduzir");
    return true;   // carregou, mas com aviso
  }
  (void)ppg2;
  return true;
}

// ---------------------------------------------------------------------
// CONFIGURACAO - texto. Carregar preenche a area de preparo e reaproveita
// o caminho ja existente de CMD_APLICAR_CONFIG, que valida modo manual,
// recalcula a resolucao e grava no NVS.
// ---------------------------------------------------------------------
static bool salvarConfig(const char* nome) {
  char caminho[64];
  caminhoDe(caminho, sizeof(caminho), TIPO_CFG, nome);

  File f = SD.open(caminho, FILE_WRITE);
  if (!f) return false;
  const ConfigPendente& c = configPendente;
  f.println("ROBO2DOF-CFG 1");
  f.printf("velN=%lu\n",  (unsigned long)c.velNormal);
  f.printf("velP=%lu\n",  (unsigned long)c.velPrecisao);
  f.printf("velA=%lu\n",  (unsigned long)c.velAuto);
  f.printf("velC=%.3f\n", c.velCordaoMmS);
  f.printf("acel1=%lu\n", (unsigned long)c.acel1);
  f.printf("acel2=%lu\n", (unsigned long)c.acel2);
  f.printf("ppv1=%lu\n",  (unsigned long)c.ppv1);
  f.printf("ppv2=%lu\n",  (unsigned long)c.ppv2);
  f.printf("red1=%.4f\n", c.red1);
  f.printf("red2=%.4f\n", c.red2);
  f.printf("escala=%u\n", (unsigned)c.escalaTraj);
  f.printf("l1=%.3f\n",   c.elo1);
  f.printf("l2=%.3f\n",   c.elo2);
  f.printf("dobra=%.3f\n",c.folgaDobra);
  f.printf("envY=%.3f\n", c.envY);
  f.printf("envR=%.3f\n", c.envRaio);
  f.printf("pCur=%u\n",   c.protCurso    ? 1u : 0u);
  f.printf("pDob=%u\n",   c.protDobra    ? 1u : 0u);
  f.printf("pEnv=%u\n",   c.protEnvelope ? 1u : 0u);
  // Referencia: nao e recarregado, serve para conferir o arquivo.
  f.printf("# curso J1 %.2f a %.2f graus\n", J1.grausMin, J1.grausMax);
  f.printf("# curso J2 %.2f a %.2f graus\n", J2.grausMin, J2.grausMax);
  f.close();
  return true;
}

static bool carregarConfig(const char* nome, char* erro, size_t tamErro) {
  char caminho[64];
  caminhoDe(caminho, sizeof(caminho), TIPO_CFG, nome);

  File f = SD.open(caminho, FILE_READ);
  if (!f) { snprintf(erro, tamErro, "arquivo nao encontrado"); return false; }

  // Parte do estado vivo: chave ausente no arquivo mantem o valor atual.
  prepararConfigPendente();
  ConfigPendente& c = configPendente;

  char linha[80];
  bool cabecalhoOk = false;
  while (f.available()) {
    size_t k = 0;
    while (f.available() && k < sizeof(linha) - 1) {
      const char ch = (char)f.read();
      if (ch == '\n') break;
      if (ch != '\r') linha[k++] = ch;
    }
    linha[k] = '\0';
    if (k == 0 || linha[0] == '#') continue;
    if (!cabecalhoOk) {
      if (strncmp(linha, "ROBO2DOF-CFG", 11) != 0) {
        f.close(); snprintf(erro, tamErro, "nao e uma configuracao do Robo2dof");
        return false;
      }
      cabecalhoOk = true;
      continue;
    }
    char* igual = strchr(linha, '=');
    if (!igual) continue;
    *igual = '\0';
    const char* ch = linha;
    const char* vs = igual + 1;
    const double v = atof(vs);

    if      (!strcmp(ch, "velN"))   c.velNormal    = (uint32_t)v;
    else if (!strcmp(ch, "velP"))   c.velPrecisao  = (uint32_t)v;
    else if (!strcmp(ch, "velA"))   c.velAuto      = (uint32_t)v;
    else if (!strcmp(ch, "velC"))   c.velCordaoMmS = (float)v;
    else if (!strcmp(ch, "acel1"))  c.acel1        = (uint32_t)v;
    else if (!strcmp(ch, "acel2"))  c.acel2        = (uint32_t)v;
    else if (!strcmp(ch, "ppv1"))   c.ppv1         = (uint32_t)v;
    else if (!strcmp(ch, "ppv2"))   c.ppv2         = (uint32_t)v;
    else if (!strcmp(ch, "red1"))   c.red1         = (float)v;
    else if (!strcmp(ch, "red2"))   c.red2         = (float)v;
    else if (!strcmp(ch, "escala")) c.escalaTraj   = (uint16_t)v;
    else if (!strcmp(ch, "l1"))     c.elo1         = (float)v;
    else if (!strcmp(ch, "l2"))     c.elo2         = (float)v;
    else if (!strcmp(ch, "dobra"))  c.folgaDobra   = (float)v;
    else if (!strcmp(ch, "envY"))   c.envY         = (float)v;
    else if (!strcmp(ch, "envR"))   c.envRaio      = (float)v;
    else if (!strcmp(ch, "pCur"))   c.protCurso    = (v != 0);
    else if (!strcmp(ch, "pDob"))   c.protDobra    = (v != 0);
    else if (!strcmp(ch, "pEnv"))   c.protEnvelope = (v != 0);
  }
  f.close();
  if (!cabecalhoOk) { snprintf(erro, tamErro, "arquivo vazio ou ilegivel"); return false; }

  // Mesma validacao do handler HTTP: arquivo do cartao nao e mais
  // confiavel que um POST vindo do navegador.
  if (c.velNormal == 0 || c.velPrecisao == 0 || c.velAuto == 0 ||
      c.velCordaoMmS <= 0 || c.acel1 == 0 || c.acel2 == 0 ||
      c.ppv1 == 0 || c.ppv2 == 0 || c.red1 <= 0 || c.red2 <= 0 ||
      c.elo1 <= 0 || c.elo2 <= 0 ||
      c.velNormal > FREQ_PULSO_MAX_HZ || c.velPrecisao > FREQ_PULSO_MAX_HZ ||
      c.velAuto > FREQ_PULSO_MAX_HZ ||
      c.folgaDobra < 0 || c.folgaDobra > 90 || c.envRaio < 0) {
    prepararConfigPendente();   // descarta o que foi lido
    snprintf(erro, tamErro, "configuracao com valor fora de faixa");
    return false;
  }
  if (c.escalaTraj < 10)  c.escalaTraj = 10;
  if (c.escalaTraj > 200) c.escalaTraj = 200;
  return true;
}

// ---------------------------------------------------------------------
static void listar(ArmTipo tipo) {
  listaN    = 0;
  listaTipo = tipo;

  File dir = SD.open(pastaDe(tipo));
  if (!dir) return;

  const size_t tamExt = strlen(extDe(tipo));
  for (File f = dir.openNextFile(); f && listaN < MAX_ARQ_LISTA; f = dir.openNextFile()) {
    if (f.isDirectory()) { f.close(); continue; }
    const char* nm = f.name();
    const char* base = strrchr(nm, '/');
    base = base ? base + 1 : nm;

    const size_t n = strlen(base);
    if (n <= tamExt || strcasecmp(base + n - tamExt, extDe(tipo)) != 0) { f.close(); continue; }

    size_t corte = n - tamExt;
    if (corte > MAX_NOME_ARQ) corte = MAX_NOME_ARQ;
    memcpy(lista[listaN].nome, base, corte);
    lista[listaN].nome[corte] = '\0';
    lista[listaN].bytes = (uint32_t)f.size();
    listaN++;
    f.close();
  }
  dir.close();
}

static void escoarLog() {
  if (!montado || !arquivoLog[0] || !filaLog) return;
  LinhaLog l;
  if (uxQueueMessagesWaiting(filaLog) == 0) return;

  File f = SD.open(arquivoLog, FILE_APPEND);
  if (!f) return;
  uint8_t k = 0;
  while (k < 16 && xQueueReceive(filaLog, &l, 0) == pdTRUE) {
    f.printf("%lu;%s\n", (unsigned long)l.ms, l.txt);
    k++;
  }
  f.close();
}

// ---------------------------------------------------------------------
static void executar(const Pedido& p) {
  char erro[64] = "";

  if (!montado && p.tarefa != TAR_MONTAR) {
    definirResultado(ARM_SEM_CARTAO, "nenhum cartao no slot");
    return;
  }

  switch (p.tarefa) {
    case TAR_MONTAR:
      // Sempre desmonta antes: o modulo de 6 pinos nao tem sinal de
      // deteccao, entao trocar de cartao nao avisa ninguem. Sem forcar,
      // montar() veria "ja montado" e devolveria o cartao antigo.
      desmontar();
      if (montar()) definirResultado(ARM_PRONTO, "cartao montado");
      else          definirResultado(ARM_SEM_CARTAO, "nenhum cartao detectado");
      break;

    case TAR_LISTAR:
      listar(armTipoDe(p.nome));
      definirResultado(ARM_PRONTO, "%u arquivo(s)", (unsigned)listaN);
      break;

    case TAR_APAGAR: {
      // p.nome chega como "<tipo>/<arquivo>"
      char tipoTxt[8] = "";
      const char* barra = strchr(p.nome, '/');
      if (!barra) { definirResultado(ARM_PRONTO, "nome invalido"); break; }
      const size_t nt = (size_t)(barra - p.nome);
      if (nt >= sizeof(tipoTxt)) { definirResultado(ARM_PRONTO, "nome invalido"); break; }
      memcpy(tipoTxt, p.nome, nt); tipoTxt[nt] = '\0';
      const ArmTipo t = armTipoDe(tipoTxt);
      if (t == TIPO_INVALIDO || !armNomeValido(barra + 1)) {
        definirResultado(ARM_PRONTO, "nome invalido"); break;
      }
      char caminho[64];
      caminhoDe(caminho, sizeof(caminho), t, barra + 1);
      if (SD.remove(caminho)) { listar(t); definirResultado(ARM_PRONTO, "apagado"); }
      else                     definirResultado(ARM_PRONTO, "nao consegui apagar");
      break;
    }

    case TAR_SALVAR_PROG:
      if (salvarPrograma(p.nome)) {
        atualizarEspaco(); listar(TIPO_PROG);
        logEvento("programa salvo: %s (%u pontos)", p.nome, (unsigned)stagingN);
        definirResultado(ARM_PRONTO, "programa \"%s\" salvo", p.nome);
      } else {
        // Falha ao ABRIR para escrita quase sempre e cartao retirado ou
        // travado. Desmonta para a retentativa periodica pegar de volta
        // quando ele voltar ao slot.
        desmontar();
        definirResultado(ARM_ERRO, "nao consegui gravar o programa");
      }
      break;

    case TAR_CARREGAR_PROG:
      if (carregarPrograma(p.nome, erro, sizeof(erro))) {
        // Nao mexe no programa vivo: quem aplica e o core 1.
        enviarComandoNomeado(CMD_ARQ_APLICAR_PROG, p.nome);
        definirResultado(ARM_PRONTO, "\"%s\": %u pontos lidos", p.nome, (unsigned)stagingN);
      } else {
        definirResultado(ARM_ERRO, "%s", erro);
      }
      break;

    case TAR_SALVAR_TRAJ:
      if (salvarTrajetoria(p.nome)) {
        atualizarEspaco(); listar(TIPO_TRAJ);
        logEvento("trajetoria salva: %s (%u pontos)", p.nome, (unsigned)trajPontos());
        definirResultado(ARM_PRONTO, "trajetoria \"%s\" salva", p.nome);
      } else {
        desmontar();
        definirResultado(ARM_ERRO, "nao consegui gravar a trajetoria");
      }
      enviarComando(CMD_ARQ_LIBERAR_TRAJ);
      break;

    case TAR_CARREGAR_TRAJ:
      if (carregarTrajetoria(p.nome, erro, sizeof(erro))) {
        if (erro[0]) definirResultado(ARM_PRONTO, "%s", erro);   // carregou com aviso
        else         definirResultado(ARM_PRONTO, "trajetoria \"%s\" carregada (%u pontos)",
                                      p.nome, (unsigned)trajPontos());
        logEvento("trajetoria carregada: %s", p.nome);
      } else {
        definirResultado(ARM_ERRO, "%s", erro);
      }
      enviarComando(CMD_ARQ_LIBERAR_TRAJ);
      break;

    case TAR_SALVAR_CONFIG:
      if (salvarConfig(p.nome)) {
        atualizarEspaco(); listar(TIPO_CFG);
        definirResultado(ARM_PRONTO, "ajustes salvos em \"%s\"", p.nome);
      } else {
        desmontar();
        definirResultado(ARM_ERRO, "nao consegui gravar os ajustes");
      }
      break;

    case TAR_CARREGAR_CONFIG:
      if (carregarConfig(p.nome, erro, sizeof(erro))) {
        // Reaproveita o caminho ja existente: o core 1 valida modo
        // manual, recalcula a resolucao e grava no NVS.
        enviarComando(CMD_APLICAR_CONFIG);
        definirResultado(ARM_PRONTO, "ajustes de \"%s\" enviados", p.nome);
      } else {
        definirResultado(ARM_ERRO, "%s", erro);
      }
      break;

    default:
      definirResultado(ARM_PRONTO, "nada a fazer");
      break;
  }
}

// Um ciclo da tarefa. Fica separado do laco infinito para que o banco de
// testes consiga executar a tarefa passo a passo, sem precisar de thread.
static bool armPrimeiroCiclo = true;

static void armCiclo(uint32_t esperaMs) {
  static uint32_t ultimaTentativa = 0;
  static uint32_t ultimoEscoar    = 0;

  if (armPrimeiroCiclo) {
    armPrimeiroCiclo = false;
    if (montar()) definirResultado(ARM_PRONTO, "cartao montado");
    else          definirResultado(ARM_SEM_CARTAO, "nenhum cartao detectado");
    ultimaTentativa = millis();
    ultimoEscoar    = millis();
    return;
  }

  Pedido p;
  if (xQueueReceive(filaPedidos, &p, pdMS_TO_TICKS(esperaMs)) == pdTRUE) {
    executar(p);
    return;
  }

  // Cartao inserido depois do boot: o modulo de 6 pinos nao tem sinal de
  // deteccao, entao a unica forma de perceber e tentar montar.
  if (!montado && (millis() - ultimaTentativa > 3000)) {
    ultimaTentativa = millis();
    if (montar()) definirResultado(ARM_PRONTO, "cartao montado");
  }

  if (montado && (millis() - ultimoEscoar > 1000)) {
    ultimoEscoar = millis();
    escoarLog();
  }
}

#ifdef ROBO2DOF_TESTE
void armCicloTeste() { armCiclo(0); }

// Devolve o modulo ao estado de boot. No robo isso nunca acontece (ha um
// boot so); no banco de testes cada cenario precisa comecar do zero.
void armReiniciarTeste() {
  armPrimeiroCiclo = true;
  montado = false;
  estado  = ARM_DESLIGADO;
  listaN  = 0;
  stagingN = 0;
  arquivoLog[0] = '\0';
  if (filaPedidos) { vQueueDelete(filaPedidos); filaPedidos = nullptr; }
  if (filaLog)     { vQueueDelete(filaLog);     filaLog     = nullptr; }
}
#endif

static void tarefaCartao(void* v) {
  (void)v;
  for (;;) armCiclo(100);
}

void armIniciar() {
  // proximaSessao() abre o NVS, e o objeto Preferences tem um dono so
  // (core 1). Por isso o contador e lido AQUI, no setup, antes de a
  // tarefa existir - nunca de dentro dela.
  sessao = proximaSessao();

  filaPedidos = xQueueCreate(4, sizeof(Pedido));
  filaLog     = xQueueCreate(24, sizeof(LinhaLog));
  estado      = ARM_SEM_CARTAO;
  snprintf(mensagem, sizeof(mensagem), "procurando cartao");
  // Prioridade 1, mesmo nivel da tarefa de rede: as duas dividem o core
  // 0 e nenhuma pode passar na frente do core 1.
  xTaskCreatePinnedToCore(tarefaCartao, "cartao", 8192, nullptr, 1, nullptr, 0);
}

#else   // ------------------------------------------------ sem cartao

void armIniciar() {
  estado = ARM_DESLIGADO;
  snprintf(mensagem, sizeof(mensagem), "compilado sem suporte a cartao");
}

#endif
