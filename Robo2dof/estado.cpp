#include "estado.h"
#include "armazenamento.h"   // logEvento no apagar tudo
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
  true, ENC_BAUD_PADRAO, ENC_PARIDADE_PADRAO, ENC_FUNCAO_PADRAO,
  ENC_PERIODO_PADRAO, true, ENC_BAIXA_PRIMEIRO,
  // Junta 2 nasce com registrador 0 = NAO LIGADA. Perguntar a um driver
  // que nao existe so gasta metade do barramento com tempo esgotado, e
  // enche a tela de falha que nao e falha.
  {1, 2}, {ENC_REG_PADRAO, 0},
  {ENC_CONTAGENS_PADRAO, ENC_CONTAGENS_PADRAO}
};
// Assentamento: ligado de fabrica, com numeros conservadores.
//
// 0,10 grau de tolerancia: abaixo disso o retoque seria menor que a
// propria resolucao de leitura em muitas montagens, e o eixo ficaria
// cutucando sem parar.
//
// 3 graus de teto: acima disso o problema nao e folga. E acoplamento
// solto, registrador errado ou reducao errada -- e empurrar o braco tres
// graus achando que esta consertando e o jeito mais rapido de bater a
// ferramenta em alguma coisa.
ConfigCorrecao configCorrecao = {
  true,    // ativa
  0.10f,   // toleranciaGraus
  3.00f,   // maxCorrecaoGraus
  3,       // tentativas
  true,    // vigiar
  1.00f,   // alertaGraus
};

// A maquina se localiza sozinha ao ligar, e vai para o zero.
//
// Ir para o zero SO acontece depois que o operador habilita os servos --
// que e uma acao explicita dele, na tela. E o intertravamento natural:
// enquanto ninguem habilitar, o braco nao tem como andar, por mais que
// esta chave esteja ligada.
ConfigZero configZero = { true, true, 0.30f, {false, false} };

// O habilita, agora que ele mora no barramento e nao num pino. Os
// padroes sao os PROVADOS na bancada desta maquina (teste_rs485, modos
// d/d2/s): P098 do painel = registrador 98, 1 habilita, 0 desabilita,
// pela funcao 06.
ConfigSon configSon = {
  SON_REG_PADRAO, SON_VAL_LIGA_PADRAO, SON_VAL_DESL_PADRAO, false
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

  // A area de preparo e tambem a forma canonica do backup: quem a
  // preenche a partir do estado vivo esta descrevendo a maquina inteira,
  // e a calibracao faz parte dela.
  configPendente.temCalib = true;
  configPendente.cal1  = J1.calibrada;   configPendente.cal2  = J2.calibrada;
  configPendente.p1Min = J1.passosMin;   configPendente.p1Max = J1.passosMax;
  configPendente.p2Min = J2.passosMin;   configPendente.p2Max = J2.passosMax;
  configPendente.home1 = J1.grausHome;   configPendente.home2 = J2.grausHome;

  configPendente.temMesa      = true;
  configPendente.mesaDefinida = areaMesa.definida;
  configPendente.mesaCantos   = areaMesa.cantos;
  configPendente.mesaXMin     = areaMesa.xMin;
  configPendente.mesaXMax     = areaMesa.xMax;
  configPendente.mesaYMin     = areaMesa.yMin;
  configPendente.mesaYMax     = areaMesa.yMax;
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

  // A calibracao so e tocada quando o arquivo REALMENTE tem uma. Um
  // backup gravado pela versao anterior nao carrega estes campos, e
  // aplicar zeros ali apagaria o curso medido da maquina.
  if (configPendente.temCalib) {
    J1.calibrada = configPendente.cal1;
    J2.calibrada = configPendente.cal2;
    J1.passosMin = configPendente.p1Min;  J1.passosMax = configPendente.p1Max;
    J2.passosMin = configPendente.p2Min;  J2.passosMax = configPendente.p2Max;
    J1.grausHome = configPendente.home1;
    J2.grausHome = configPendente.home2;
  }

  // Mesma regra da calibracao: so mexe quando o arquivo REALMENTE traz
  // uma mesa. Backup antigo nao apaga a area ensinada.
  if (configPendente.temMesa) {
    areaMesa.definida = configPendente.mesaDefinida;
    areaMesa.cantos   = configPendente.mesaCantos;
    areaMesa.xMin     = configPendente.mesaXMin;
    areaMesa.xMax     = configPendente.mesaXMax;
    areaMesa.yMin     = configPendente.mesaYMin;
    areaMesa.yMax     = configPendente.mesaYMax;
  }

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
  areaMesa.definida = prefs.getBool ("mesaOn", false);
  areaMesa.cantos   = (uint8_t)prefs.getUInt("mesaN", 0);
  areaMesa.xMin     = prefs.getFloat("mesaX0", 0.0f);
  areaMesa.xMax     = prefs.getFloat("mesaX1", 0.0f);
  areaMesa.yMin     = prefs.getFloat("mesaY0", 0.0f);
  areaMesa.yMax     = prefs.getFloat("mesaY1", 0.0f);

  producao.ciclosTotais    = prefs.getUInt("ciclos", 0);
  producao.abortados       = prefs.getUInt("cicAb",  0);
  producao.horasArcoS      = prefs.getUInt("arcoS",  0);
  producao.desdeManutencao = prefs.getUInt("cicMan", 0);

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

  configEncoder.ativo        = prefs.getBool ("encOn",  true);
  configCorrecao.ativa            = prefs.getBool ("crOn",  true);
  configZero.sincronizar          = prefs.getBool ("zrSin", true);
  configZero.irParaZero           = prefs.getBool ("zrIr",  true);
  configZero.toleranciaGraus      = prefs.getFloat("zrTol", 0.30f);
  configZero.ensinado[0]          = prefs.getBool("zrEn1", false);
  configZero.ensinado[1]          = prefs.getBool("zrEn2", false);
  encoderCarregarReferencia(1, prefs.getInt("encRf1", 0));
  encoderCarregarReferencia(2, prefs.getInt("encRf2", 0));
  if (configZero.toleranciaGraus < 0.05f) configZero.toleranciaGraus = 0.05f;
  if (configZero.toleranciaGraus > 10.0f) configZero.toleranciaGraus = 10.0f;
  configCorrecao.vigiar           = prefs.getBool ("crVig", true);
  configCorrecao.toleranciaGraus  = prefs.getFloat("crTol", 0.10f);
  configCorrecao.maxCorrecaoGraus = prefs.getFloat("crMax", 3.00f);
  configCorrecao.alertaGraus      = prefs.getFloat("crAlr", 1.00f);
  configCorrecao.tentativas       = (uint8_t)prefs.getUInt("crTent", 3);
  // Numeros fora de faixa gravados por engano nao podem virar retoque
  // gigante nem laco infinito.
  if (configCorrecao.toleranciaGraus  < 0.01f) configCorrecao.toleranciaGraus  = 0.01f;
  if (configCorrecao.maxCorrecaoGraus > 15.0f) configCorrecao.maxCorrecaoGraus = 15.0f;
  if (configCorrecao.maxCorrecaoGraus < configCorrecao.toleranciaGraus)
      configCorrecao.maxCorrecaoGraus = configCorrecao.toleranciaGraus;
  if (configCorrecao.tentativas > 10) configCorrecao.tentativas = 10;
  configEncoder.baud         = prefs.getUInt ("encBd",  ENC_BAUD_PADRAO);
  configEncoder.paridade     = (uint8_t) prefs.getUInt("encPar", ENC_PARIDADE_PADRAO);
  configEncoder.funcao       = (uint8_t) prefs.getUInt("encFn",  ENC_FUNCAO_PADRAO);
  if (configEncoder.funcao != 3 && configEncoder.funcao != 4)
    configEncoder.funcao = ENC_FUNCAO_PADRAO;
  configEncoder.periodoMs    = (uint16_t)prefs.getUInt("encPer", ENC_PERIODO_PADRAO);
  configEncoder.trintaEDois  = prefs.getBool ("enc32",  true);
  configEncoder.baixaPrimeiro= prefs.getBool ("encLo",  ENC_BAIXA_PRIMEIRO);
  configEncoder.id[0]        = (uint8_t) prefs.getUInt("encId1", 1);
  configEncoder.id[1]        = (uint8_t) prefs.getUInt("encId2", 2);
  configEncoder.reg[0]       = (uint16_t)prefs.getUInt("encRg1", ENC_REG_PADRAO);
  configEncoder.reg[1]       = (uint16_t)prefs.getUInt("encRg2", 0);
  configEncoder.contagensPorVolta[0] = prefs.getFloat("encCv1", ENC_CONTAGENS_PADRAO);
  configEncoder.contagensPorVolta[1] = prefs.getFloat("encCv2", ENC_CONTAGENS_PADRAO);
  configSon.reg        = (uint16_t)prefs.getUInt("sonRg", SON_REG_PADRAO);
  configSon.valLiga    = (uint16_t)prefs.getUInt("sonVL", SON_VAL_LIGA_PADRAO);
  configSon.valDesliga = (uint16_t)prefs.getUInt("sonVD", SON_VAL_DESL_PADRAO);
  configSon.funcao16   = prefs.getBool("sonF16", false);
  // Registrador 0 e o inicio da tabela de parametros, nunca a posicao.
  // Um 0 gravado por uma versao anterior significa "nunca foi
  // configurado" -- vale o padrao medido, nao um endereco que so pode
  // devolver parametro.
  // Junta 1 com 0 gravado por versao anterior vira o padrao medido; a
  // junta 2 pode ficar em 0 de proposito, que quer dizer "nao ligada".
  if (configEncoder.reg[0] == 0) configEncoder.reg[0] = ENC_REG_PADRAO;
  for (uint8_t i = 0; i < 2; i++)
    if (configEncoder.contagensPorVolta[i] < 1.0f)
      configEncoder.contagensPorVolta[i] = ENC_CONTAGENS_PADRAO;
  encoderPendente = configEncoder;

  prefs.end();

  recalcularResolucao();
  prepararConfigPendente();   // a area de preparo nasce coerente com o vivo

  Serial.println("[NVS] Configuracoes carregadas.");
}

// =====================================================================
//  Producao
// =====================================================================
// De fabrica sem mesa ensinada: a maquina se comporta como antes, com o
// Y minimo e o raio morto. A mesa so passa a proteger quando alguem a
// ensinou -- protecao inventada por padrao recusaria movimento valido na
// primeira vez que a maquina liga.
AreaMesa areaMesa = {false, 0, 0.0f, 0.0f, 0.0f, 0.0f};

void mesaEnsinarCanto(float x, float y) {
  if (areaMesa.cantos == 0) {
    areaMesa.xMin = areaMesa.xMax = x;
    areaMesa.yMin = areaMesa.yMax = y;
  } else {
    if (x < areaMesa.xMin) areaMesa.xMin = x;
    if (x > areaMesa.xMax) areaMesa.xMax = x;
    if (y < areaMesa.yMin) areaMesa.yMin = y;
    if (y > areaMesa.yMax) areaMesa.yMax = y;
  }
  if (areaMesa.cantos < 255) areaMesa.cantos++;
  // Dois cantos na mesma linha nao fazem retangulo: enquanto a area for
  // uma risca, a protecao nao vale -- ela recusaria tudo.
  areaMesa.definida = (areaMesa.cantos >= 2) &&
                      (areaMesa.xMax - areaMesa.xMin > 10.0f) &&
                      (areaMesa.yMax - areaMesa.yMin > 10.0f);
}

void mesaLimpar() {
  areaMesa = AreaMesa{false, 0, 0.0f, 0.0f, 0.0f, 0.0f};
}

Producao producao = {0, 0, 0, 0, 0};

// A gravacao no NVS e por ciclo, nao por segundo: a memoria do ESP32
// aguenta na casa de 100 mil escritas por celula, e um contador salvo a
// cada volta do laco a queimaria em uma tarde.
static void salvarProducao() {
  prefs.begin("robo2dof", false);
  prefs.putUInt("ciclos", producao.ciclosTotais);
  prefs.putUInt("cicAb",  producao.abortados);
  prefs.putUInt("arcoS",  producao.horasArcoS);
  prefs.putUInt("cicMan", producao.desdeManutencao);
  prefs.end();
}

void producaoContarCiclo(bool concluido) {
  if (concluido) {
    producao.ciclosTotais++;
    producao.ciclosSessao++;
    producao.desdeManutencao++;
  } else {
    producao.abortados++;
  }
  salvarProducao();
}

void producaoZerarManutencao() {
  producao.desdeManutencao = 0;
  salvarProducao();
}

// Somado pelo rele. Guardado em segundos inteiros para nao gravar no NVS
// a cada arco curto -- o resto de milissegundos fica de fora, e num
// numero que so serve para dizer "troque o bico" isso nao muda nada.
void producaoSomarArco(uint32_t ms) {
  static uint32_t restoMs = 0;
  restoMs += ms;
  if (restoMs >= 1000) {
    producao.horasArcoS += restoMs / 1000;
    restoMs %= 1000;
  }
}

uint32_t proximaSessao() {
  prefs.begin("robo2dof", false);
  const uint32_t s = prefs.getUInt("sessao", 0) + 1;
  prefs.putUInt("sessao", s);
  prefs.end();
  return s;
}

volatile bool configSujaParaCartao = false;

void salvarConfiguracoes() {
  // Ver o bloco em estado.h: aqui so marca; quem grava no cartao e o
  // core 1, juntando as marcas.
  configSujaParaCartao = true;
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
  prefs.putBool ("crOn",   configCorrecao.ativa);
  prefs.putBool ("zrSin",  configZero.sincronizar);
  prefs.putBool ("zrIr",   configZero.irParaZero);
  prefs.putFloat("zrTol",  configZero.toleranciaGraus);
  // A referencia absoluta do encoder e a unica calibracao que sobra com
  // encoder absoluto: ensinada uma vez, vale para sempre.
  prefs.putBool ("zrEn1",  configZero.ensinado[0]);
  prefs.putBool ("zrEn2",  configZero.ensinado[1]);
  prefs.putBool ("mesaOn", areaMesa.definida);
  prefs.putUInt ("mesaN",  areaMesa.cantos);
  prefs.putFloat("mesaX0", areaMesa.xMin);
  prefs.putFloat("mesaX1", areaMesa.xMax);
  prefs.putFloat("mesaY0", areaMesa.yMin);
  prefs.putFloat("mesaY1", areaMesa.yMax);
  prefs.putInt  ("encRf1", encoderReferencia(1));
  prefs.putInt  ("encRf2", encoderReferencia(2));
  prefs.putBool ("crVig",  configCorrecao.vigiar);
  prefs.putFloat("crTol",  configCorrecao.toleranciaGraus);
  prefs.putFloat("crMax",  configCorrecao.maxCorrecaoGraus);
  prefs.putFloat("crAlr",  configCorrecao.alertaGraus);
  prefs.putUInt ("crTent", configCorrecao.tentativas);
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
  prefs.putUInt ("sonRg",  configSon.reg);
  prefs.putUInt ("sonVL",  configSon.valLiga);
  prefs.putUInt ("sonVD",  configSon.valDesliga);
  prefs.putBool ("sonF16", configSon.funcao16);

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

// Grava a copia no cartao quando ha marca, o cartao esta livre e ja
// passou tempo suficiente desde a ultima. Chamada do core 1, uma vez por
// ciclo -- e ela sai na hora quando nao ha o que fazer.
//
// O NOME E FIXO. Copia de configuracao nao e biblioteca: uma so, sempre
// a mesma, sempre a mais recente. Nome escolhido pelo operador viraria
// uma pasta de "config", "config2", "config_bom" e ninguem saberia qual
// esta valendo.
void configCopiarParaCartaoSePreciso() {
  static uint32_t ultimaMs = 0;
  if (!configSujaParaCartao) return;
  if (armEstado() != ARM_PRONTO || armOcupado()) return;
  const uint32_t agora = millis();
  if (ultimaMs && (agora - ultimaMs) < CFG_CARTAO_INTERVALO_MS) return;
  prepararConfigPendente();          // forma canonica da configuracao viva
  if (!armSolicitar(TAR_SALVAR_CONFIG, CFG_CARTAO_NOME)) return;
  ultimaMs = agora;
  configSujaParaCartao = false;
}

// Ver o bloco em estado.h para o que isto apaga e o que nao apaga.
void apagarTudo() {
  logEvento("APAGAR TUDO: NVS limpo, reiniciando");
  // A copia no cartao e memoria da MAQUINA, entao vai junto -- senao a
  // maquina "apagada" voltaria a se configurar sozinha na proxima
  // gravacao. Programas e trajetorias no cartao NAO sao tocados: sao
  // trabalho do operador.
  configSujaParaCartao = false;
  if (armEstado() == ARM_PRONTO && !armOcupado())
    armSolicitar(TAR_APAGAR, CFG_CARTAO_NOME);
  Serial.println("[NVS] Apagando TUDO e reiniciando.");
  prefs.begin("robo2dof", false);
  prefs.clear();
  prefs.end();
  // Sem o atraso a mensagem nao chega ao navegador nem ao monitor serial,
  // e o operador ve a maquina reiniciar sem nenhuma confirmacao do que
  // aconteceu.
  delay(300);
  ESP.restart();
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
  // A mesa ensinada NAO e apagada aqui. "Restaurar padroes" devolve
  // parametros de fabrica; a area util e uma medida da instalacao, do
  // mesmo tipo da calibracao -- e apagar meia hora de trabalho de quem so
  // queria voltar as velocidades seria uma armadilha.

  recalcularResolucao();
  prepararConfigPendente();
  salvarConfiguracoes();
}
