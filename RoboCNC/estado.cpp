#include "estado.h"
#include "encoder.h"
#include <Preferences.h>
#include <stdarg.h>
#include <string.h>

Junta J1;
Junta J2;

float velNormal      = VEL_NORMAL_PADRAO;
float velPrecisao    = VEL_PRECISAO_PADRAO;
float velAuto        = VEL_AUTO_PADRAO;
float velCordaoMmS   = VEL_CORDAO_PADRAO;

float elo1Mm     = ELO1_PADRAO_MM;
float elo2Mm     = ELO2_PADRAO_MM;
float folgaDobra = FOLGA_DOBRA_PADRAO;
float envYMin    = ENV_Y_MIN_PADRAO;
float envRaioMin = ENV_RAIO_MIN_PADRAO;

bool protCurso    = PROT_CURSO_PADRAO;
bool protDobra    = PROT_DOBRA_PADRAO;
bool protEnvelope = PROT_ENVELOPE_PADRAO;

uint16_t escalaVelocidadeTraj = 100;
uint8_t  suavidadePartida     = SUAVIDADE_PADRAO;

ConfigEncoder configEncoder = {
  false, ENC_BAUD_PADRAO, ENC_PARIDADE_PADRAO, ENC_FUNCAO_PADRAO,
  ENC_PERIODO_PADRAO, true, false, {1, 2}, {0, 0}, {10000.0f, 10000.0f}
};
ConfigEncoder encoderPendente = configEncoder;

Modo        modoAtual     = MODO_MANUAL;
EstadoCalib estadoCalib   = CAL_INATIVO;
bool        modoPrecisao  = false;
bool        servosLigados = false;
char        ultimaMensagem[96] = "Sistema iniciado";

QueueHandle_t filaComandos = nullptr;

volatile bool pedidoParada    = false;
bool          movimentoLiberado = false;

ConfigPendente configPendente;

volatile uint32_t ultimoContatoOperadorMs = 0;

static Snapshot      snapshotAtual;
static portMUX_TYPE  muxSnapshot = portMUX_INITIALIZER_UNLOCKED;
static Preferences   prefs;

// ---------------------------------------------------------------------
// A resolucao de cada junta vem da engrenagem eletronica do driver
// multiplicada pela reducao mecanica daquele eixo. Sao numeros
// CONHECIDOS, nao medidos. Os limites em graus sao reconvertidos aqui
// para nunca ficarem incoerentes com a resolucao vigente.
void recalcularResolucao() {
  Junta* js[2] = { &J1, &J2 };
  for (uint8_t i = 0; i < 2; i++) {
    Junta& j = *js[i];
    j.passosPorGrau = (j.passosPorVolta * j.reducao) / 360.0f;
    if (j.passosPorGrau > 0.0f) {
      j.grausMin = j.passosMin / j.passosPorGrau + j.grausHome;
      j.grausMax = j.passosMax / j.passosPorGrau + j.grausHome;
    }
  }
}

// ---------------------------------------------------------------------
bool enviarComando(TipoComando tipo, int32_t a, int32_t b, float f1, float f2) {
  if (!filaComandos) return false;
  Comando c;
  c.tipo = tipo; c.a = a; c.b = b; c.f1 = f1; c.f2 = f2;
  c.nome[0] = '\0';
  return xQueueSend(filaComandos, &c, 0) == pdTRUE;
}

bool enviarComandoNomeado(TipoComando tipo, const char* nome,
                          int32_t a, int32_t b) {
  if (!filaComandos) return false;
  Comando c;
  c.tipo = tipo; c.a = a; c.b = b; c.f1 = 0.0f; c.f2 = 0.0f;
  strncpy(c.nome, nome ? nome : "", MAX_NOME_ARQ);
  c.nome[MAX_NOME_ARQ] = '\0';
  return xQueueSend(filaComandos, &c, 0) == pdTRUE;
}

// A parada nao passa pela fila: ver estado.h.
void solicitarParada() {
  pedidoParada = true;
}

void limparFilaComandos() {
  if (!filaComandos) return;
  Comando descarte;
  while (xQueueReceive(filaComandos, &descarte, 0) == pdTRUE) { }
}

void registrarContatoOperador() {
  ultimoContatoOperadorMs = millis();
}

// ---------------------------------------------------------------------
void publicarSnapshot(const Snapshot& s) {
  portENTER_CRITICAL(&muxSnapshot);
  snapshotAtual = s;
  portEXIT_CRITICAL(&muxSnapshot);
}

void lerSnapshot(Snapshot& destino) {
  portENTER_CRITICAL(&muxSnapshot);
  destino = snapshotAtual;
  portEXIT_CRITICAL(&muxSnapshot);
}

// definirMensagem() e chamada de dentro do laco de 1 ms do core 1 (o jog
// de recuperacao chamava a cada ciclo). Serial.print() BLOQUEIA quando o
// buffer de TX enche, e quem trava e o laco que roda supervisionar() -
// uma frase informativa passando na frente da supervisao de seguranca.
//
// A mensagem sempre atualiza (a interface ve tudo). O eco na serial e que
// e poupado: repeticao identica nao imprime, e ha um piso de 50 ms entre
// impressoes, o que limita a UART a ~9% da banda.
void definirMensagem(const char* fmt, ...) {
  static uint32_t ultimoEcoMs = 0;

  char buf[sizeof(ultimaMensagem)];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  const bool repetida = (strcmp(buf, ultimaMensagem) == 0);
  memcpy(ultimaMensagem, buf, sizeof(ultimaMensagem));
  if (repetida) return;

  const uint32_t agora = millis();
  if (ultimoEcoMs != 0 && (agora - ultimoEcoMs) < 50) return;
  ultimoEcoMs = agora;

  Serial.print("[MSG] ");
  Serial.println(ultimaMensagem);
}

// ---------------------------------------------------------------------
// Configuracao: preparo no core 0, aplicacao no core 1.
// ---------------------------------------------------------------------
void prepararConfigPendente() {
  configPendente.velNormal    = velNormal;
  configPendente.velPrecisao  = velPrecisao;
  configPendente.velAuto      = velAuto;
  configPendente.velCordaoMmS = velCordaoMmS;
  configPendente.acel1        = J1.aceleracao;
  configPendente.acel2        = J2.aceleracao;
  configPendente.ppv1         = J1.passosPorVolta;
  configPendente.ppv2         = J2.passosPorVolta;
  configPendente.red1         = J1.reducao;
  configPendente.red2         = J2.reducao;
  configPendente.inv1         = J1.inverterDir;
  configPendente.inv2         = J2.inverterDir;
  configPendente.escalaTraj   = escalaVelocidadeTraj;
  configPendente.suavidade    = suavidadePartida;
  configPendente.elo1         = elo1Mm;
  configPendente.elo2         = elo2Mm;
  configPendente.folgaDobra   = folgaDobra;
  configPendente.envY         = envYMin;
  configPendente.envRaio      = envRaioMin;
  configPendente.protCurso    = protCurso;
  configPendente.protDobra    = protDobra;
  configPendente.protEnvelope = protEnvelope;
}

void aplicarConfigPendente() {
  velNormal         = configPendente.velNormal;
  velPrecisao       = configPendente.velPrecisao;
  velAuto           = configPendente.velAuto;
  velCordaoMmS      = configPendente.velCordaoMmS;
  J1.aceleracao     = configPendente.acel1;
  J2.aceleracao     = configPendente.acel2;
  J1.passosPorVolta = configPendente.ppv1;
  J2.passosPorVolta = configPendente.ppv2;
  J1.reducao        = configPendente.red1;
  J2.reducao        = configPendente.red2;
  J1.inverterDir    = configPendente.inv1;
  J2.inverterDir    = configPendente.inv2;
  escalaVelocidadeTraj = configPendente.escalaTraj;
  suavidadePartida     = configPendente.suavidade;
  elo1Mm            = configPendente.elo1;
  elo2Mm            = configPendente.elo2;
  folgaDobra        = configPendente.folgaDobra;
  envYMin           = configPendente.envY;
  envRaioMin        = configPendente.envRaio;
  protCurso         = configPendente.protCurso;
  protDobra         = configPendente.protDobra;
  protEnvelope      = configPendente.protEnvelope;

  recalcularResolucao();
}

// ---------------------------------------------------------------------
// Persistencia. As chaves e1*/e2* sao as mesmas da versao anterior,
// entao uma calibracao ja salva continua valendo apos a atualizacao.
// ---------------------------------------------------------------------
void carregarConfiguracoes() {
  prefs.begin("robo2dof", false);

  // Chaves NOVAS: as antigas guardavam Hz, e reler 3000 como 3000 graus/s
  // seria absurdo. Quem atualiza recebe os padroes em graus/s.
  velNormal      = prefs.getFloat("velNg",       VEL_NORMAL_PADRAO);
  velPrecisao    = prefs.getFloat("velPg",       VEL_PRECISAO_PADRAO);
  velAuto        = prefs.getFloat("velAg",       VEL_AUTO_PADRAO);
  velCordaoMmS   = prefs.getFloat("velCmm",      VEL_CORDAO_PADRAO);
  protCurso      = prefs.getBool ("pCur", PROT_CURSO_PADRAO);
  protDobra      = prefs.getBool ("pDob", PROT_DOBRA_PADRAO);
  protEnvelope   = prefs.getBool ("pEnv", PROT_ENVELOPE_PADRAO);
  // Chaves antigas servem de padrao, entao uma configuracao ja salva
  // continua valendo depois da atualizacao.
  const uint32_t ppvAntigo = prefs.getUInt ("passosVolta", PASSOS_POR_VOLTA_PADRAO);
  const float    redAntiga = prefs.getFloat("reducao",     REDUCAO_PADRAO);
  J1.passosPorVolta = prefs.getUInt ("ppv1", ppvAntigo);
  J1.reducao        = prefs.getFloat("red1", redAntiga);
  J2.passosPorVolta = prefs.getUInt ("ppv2", ppvAntigo);
  J2.reducao        = prefs.getFloat("red2", redAntiga);

  J1.aceleracao  = prefs.getFloat("acel1g", ACEL_PADRAO);
  J2.aceleracao  = prefs.getFloat("acel2g", ACEL_PADRAO);
  J1.inverterDir = prefs.getBool ("inv1", false);
  J2.inverterDir = prefs.getBool ("inv2", false);
  suavidadePartida = (uint8_t)prefs.getUInt("suav", SUAVIDADE_PADRAO);

  elo1Mm     = prefs.getFloat("l1",      ELO1_PADRAO_MM);
  elo2Mm     = prefs.getFloat("l2",      ELO2_PADRAO_MM);
  folgaDobra = prefs.getFloat("dobra",   FOLGA_DOBRA_PADRAO);
  envYMin    = prefs.getFloat("envY",    ENV_Y_MIN_PADRAO);
  envRaioMin = prefs.getFloat("envR",    ENV_RAIO_MIN_PADRAO);

  J1.calibrada = prefs.getBool ("e1cal", false);
  J1.passosMin = prefs.getLong ("e1min", 0);
  J1.passosMax = prefs.getLong ("e1max", 0);
  J1.grausHome = prefs.getFloat("e1hom", 0.0f);
  J2.calibrada = prefs.getBool ("e2cal", false);
  J2.passosMin = prefs.getLong ("e2min", 0);
  J2.passosMax = prefs.getLong ("e2max", 0);
  J2.grausHome = prefs.getFloat("e2hom", 0.0f);

  configEncoder.ativo        = prefs.getBool ("encOn",  false);
  configEncoder.baud         = prefs.getUInt ("encBd",  ENC_BAUD_PADRAO);
  configEncoder.paridade     = (uint8_t) prefs.getUInt("encPar", ENC_PARIDADE_PADRAO);
  configEncoder.funcao       = (uint8_t) prefs.getUInt("encFn",  ENC_FUNCAO_PADRAO);
  configEncoder.periodoMs    = (uint16_t)prefs.getUInt("encPer", ENC_PERIODO_PADRAO);
  configEncoder.trintaEDois  = prefs.getBool ("enc32",  true);
  configEncoder.baixaPrimeiro= prefs.getBool ("encLo",  false);
  configEncoder.id[0]        = (uint8_t) prefs.getUInt("encId1", 1);
  configEncoder.id[1]        = (uint8_t) prefs.getUInt("encId2", 2);
  configEncoder.reg[0]       = (uint16_t)prefs.getUInt("encRg1", 0);
  configEncoder.reg[1]       = (uint16_t)prefs.getUInt("encRg2", 0);
  configEncoder.contagensPorVolta[0] = prefs.getFloat("encCv1", 10000.0f);
  configEncoder.contagensPorVolta[1] = prefs.getFloat("encCv2", 10000.0f);
  encoderPendente = configEncoder;

  prefs.end();

  recalcularResolucao();
  prepararConfigPendente();   // a area de preparo nasce coerente com o vivo

  Serial.println("[NVS] Configuracoes carregadas.");
}

uint32_t proximaSessao() {
  prefs.begin("robo2dof", false);
  const uint32_t s = prefs.getUInt("sessao", 0) + 1;
  prefs.putUInt("sessao", s);
  prefs.end();
  return s;
}

void salvarConfiguracoes() {
  prefs.begin("robo2dof", false);

  prefs.putFloat("velNg",       velNormal);
  prefs.putFloat("velPg",       velPrecisao);
  prefs.putFloat("velAg",       velAuto);
  prefs.putFloat("velCmm",      velCordaoMmS);
  prefs.putBool ("pCur", protCurso);
  prefs.putBool ("pDob", protDobra);
  prefs.putBool ("pEnv", protEnvelope);
  prefs.putUInt ("ppv1", J1.passosPorVolta);
  prefs.putFloat("red1", J1.reducao);
  prefs.putUInt ("ppv2", J2.passosPorVolta);
  prefs.putFloat("red2", J2.reducao);
  prefs.putFloat("acel1g",      J1.aceleracao);
  prefs.putFloat("acel2g",      J2.aceleracao);
  prefs.putBool ("inv1", J1.inverterDir);
  prefs.putBool ("inv2", J2.inverterDir);
  prefs.putUInt ("suav", suavidadePartida);

  prefs.putFloat("l1",    elo1Mm);
  prefs.putFloat("l2",    elo2Mm);
  prefs.putFloat("dobra", folgaDobra);
  prefs.putFloat("envY",  envYMin);
  prefs.putFloat("envR",  envRaioMin);

  prefs.putBool ("e1cal", J1.calibrada);
  prefs.putLong ("e1min", J1.passosMin);
  prefs.putLong ("e1max", J1.passosMax);
  prefs.putFloat("e1hom", J1.grausHome);
  prefs.putBool ("e2cal", J2.calibrada);
  prefs.putLong ("e2min", J2.passosMin);
  prefs.putLong ("e2max", J2.passosMax);
  prefs.putFloat("e2hom", J2.grausHome);

  prefs.putBool ("encOn",  configEncoder.ativo);
  prefs.putUInt ("encBd",  configEncoder.baud);
  prefs.putUInt ("encPar", configEncoder.paridade);
  prefs.putUInt ("encFn",  configEncoder.funcao);
  prefs.putUInt ("encPer", configEncoder.periodoMs);
  prefs.putBool ("enc32",  configEncoder.trintaEDois);
  prefs.putBool ("encLo",  configEncoder.baixaPrimeiro);
  prefs.putUInt ("encId1", configEncoder.id[0]);
  prefs.putUInt ("encId2", configEncoder.id[1]);
  prefs.putUInt ("encRg1", configEncoder.reg[0]);
  prefs.putUInt ("encRg2", configEncoder.reg[1]);
  prefs.putFloat("encCv1", configEncoder.contagensPorVolta[0]);
  prefs.putFloat("encCv2", configEncoder.contagensPorVolta[1]);

  prefs.end();
  Serial.println("[NVS] Configuracoes salvas.");
}


// Gravada separada do resto: mexer no encoder nao pode reescrever
// calibracao, e salvar calibracao nao pode reescrever o registrador que
// o operador levou uma tarde para achar.
void aplicarEncoderPendente() {
  configEncoder = encoderPendente;
  if (configEncoder.periodoMs < ENC_PERIODO_MIN_MS)
    configEncoder.periodoMs = ENC_PERIODO_MIN_MS;
  salvarConfiguracoes();
  encoderReconfigurar();
  definirMensagem(configEncoder.ativo
                  ? "Encoder: leitura ligada"
                  : "Encoder: leitura desligada");
}

void restaurarPadroes() {
  // Chamada apenas pelo core 1 (ver CMD_RESTAURAR_PADROES no .ino).
  velNormal      = VEL_NORMAL_PADRAO;
  velPrecisao    = VEL_PRECISAO_PADRAO;
  velAuto        = VEL_AUTO_PADRAO;
  velCordaoMmS   = VEL_CORDAO_PADRAO;
  protCurso      = PROT_CURSO_PADRAO;
  protDobra      = PROT_DOBRA_PADRAO;
  protEnvelope   = PROT_ENVELOPE_PADRAO;
  J1.passosPorVolta = PASSOS_POR_VOLTA_PADRAO;
  J1.reducao        = REDUCAO_PADRAO;
  J2.passosPorVolta = PASSOS_POR_VOLTA_PADRAO;
  J2.reducao        = REDUCAO_PADRAO;
  J1.aceleracao  = ACEL_PADRAO;
  J2.aceleracao  = ACEL_PADRAO;
  J1.inverterDir = false;
  J2.inverterDir = false;
  suavidadePartida = SUAVIDADE_PADRAO;
  elo1Mm         = ELO1_PADRAO_MM;
  elo2Mm         = ELO2_PADRAO_MM;
  folgaDobra     = FOLGA_DOBRA_PADRAO;
  envYMin        = ENV_Y_MIN_PADRAO;
  envRaioMin     = ENV_RAIO_MIN_PADRAO;
  escalaVelocidadeTraj = 100;

  recalcularResolucao();
  prepararConfigPendente();
  salvarConfiguracoes();
}
