#include "servidor_web.h"
#include "estado.h"
#include "cinematica.h"
#include "trajetoria.h"
#include "programa.h"
#include "pagina_web.h"

static WebServer server(80);

static const char* const NOMES_MODO[] = {
  "MANUAL", "GRAVANDO", "REPRODUZINDO", "EXECUTANDO",
  "POSICIONANDO", "CALIBRANDO", "FALHA"
};
static const char* const NOMES_CALIB[] = {
  "INATIVO", "HOME",
  "J1_NEG", "J1_VOLTA_NEG", "J1_POS", "J1_VOLTA_POS",
  "J2_NEG", "J2_VOLTA_NEG", "J2_POS", "J2_VOLTA_POS",
  "CONCLUIDO"
};

// ---------------------------------------------------------------------
static void ok()                    { server.send(200, "text/plain", "ok"); }
static void erro(const char* msg)   { server.send(400, "text/plain", msg); }

// Enfileirar pode falhar (fila cheia). Responder 200 nesse caso faz a
// interface acreditar que o comando foi aceito quando ele foi descartado.
static void enfileirar(TipoComando tipo, int32_t a = 0, int32_t b = 0,
                       float f1 = 0.0f, float f2 = 0.0f) {
  if (enviarComando(tipo, a, b, f1, f2)) ok();
  else server.send(503, "text/plain", "fila cheia: comando nao aceito");
}

static float argF(const char* nome, float padrao) {
  return server.hasArg(nome) ? server.arg(nome).toFloat() : padrao;
}
static long argL(const char* nome, long padrao) {
  return server.hasArg(nome) ? server.arg(nome).toInt() : padrao;
}

// ---------------------------------------------------------------------
static void handleRaiz() {
  registrarContatoWeb();
  Serial.println("[WEB] Servindo pagina de controle.");
  server.send_P(200, "text/html", PAGINA_HTML);
}

// Qualquer rota desconhecida vira log: se o navegador chegar no ESP32 e
// pedir algo inesperado, isso aparece no monitor serial em vez de sumir.
static void handleNaoEncontrado() {
  Serial.print("[WEB] Rota desconhecida: ");
  Serial.println(server.uri().c_str());
  server.send(404, "text/plain", "rota inexistente");
}

static void handleStatus() {
  registrarContatoWeb();

  Snapshot s;
  lerSnapshot(s);

  const char* modo  = (s.modo  < 7)  ? NOMES_MODO[s.modo]   : "?";
  const char* calib = (s.calib < 11) ? NOMES_CALIB[s.calib] : "?";

  uint8_t eixoCalib = 0;
  if (s.calib >= CAL_J1_NEG && s.calib <= CAL_J1_VOLTA_POS) eixoCalib = 1;
  if (s.calib >= CAL_J2_NEG && s.calib <= CAL_J2_VOLTA_POS) eixoCalib = 2;

  char json[1024];
  snprintf(json, sizeof(json),
    "{\"modo\":\"%s\",\"calib\":\"%s\",\"calibEixo\":%u,"
    "\"p1\":%ld,\"p2\":%ld,\"t1\":%.2f,\"t2\":%.2f,\"x\":%.1f,\"y\":%.1f,"
    "\"precisao\":%s,\"solda\":%s,\"servos\":%s,\"movendo\":%s,"
    "\"alarme1\":%s,\"alarme2\":%s,\"cal1\":%s,\"cal2\":%s,"
    "\"j1min\":%.1f,\"j1max\":%.1f,\"j2min\":%.1f,\"j2max\":%.1f,"
    "\"trajN\":%u,\"trajMs\":%lu,\"trajPct\":%u,\"escala\":%u,"
    "\"progN\":%u,\"progIdx\":%u,\"progPct\":%u,\"ensaio\":%s,\"velCordao\":%.1f,"
    "\"velC\":%.1f,\"protCurso\":%s,\"protDobra\":%s,\"protEnv\":%s,"
    "\"velN\":%lu,\"velP\":%lu,\"velA\":%lu,\"acel1\":%lu,\"acel2\":%lu,"
    "\"ppv1\":%lu,\"red1\":%.3f,\"ppv2\":%lu,\"red2\":%.3f,"
    "\"v1\":%.0f,\"v2\":%.0f,\"vPonta\":%.1f,\"ppg1\":%.2f,\"ppg2\":%.2f,"
    "\"l1\":%.1f,\"l2\":%.1f,\"dobra\":%.1f,\"envY\":%.1f,\"envR\":%.1f,"
    "\"msg\":\"%s\"}",
    modo, calib, (unsigned)eixoCalib,
    s.p1, s.p2, s.t1, s.t2, s.x, s.y,
    s.precisao ? "true" : "false",
    s.solda ? "true" : "false",
    s.servosLigados ? "true" : "false",
    s.emMovimento ? "true" : "false",
    s.alarme1 ? "true" : "false",
    s.alarme2 ? "true" : "false",
    s.calibrada1 ? "true" : "false",
    s.calibrada2 ? "true" : "false",
    J1.grausMin, J1.grausMax, J2.grausMin, J2.grausMax,
    (unsigned)s.trajPontos, (unsigned long)s.trajDuracaoMs,
    (unsigned)s.trajProgresso, (unsigned)escalaVelocidadeTraj,
    (unsigned)progQuantidade(), (unsigned)progIndiceAtual(),
    (unsigned)progProgresso(), progEmEnsaio() ? "true" : "false",
    velCordaoMmS, velCordaoMmS,
    protCurso ? "true" : "false", protDobra ? "true" : "false",
    protEnvelope ? "true" : "false",
    (unsigned long)velNormal, (unsigned long)velPrecisao, (unsigned long)velAuto,
    (unsigned long)J1.aceleracao, (unsigned long)J2.aceleracao,
    (unsigned long)J1.passosPorVolta, J1.reducao,
    (unsigned long)J2.passosPorVolta, J2.reducao,
    s.v1Hz, s.v2Hz, s.vPontaMmS, J1.passosPorGrau, J2.passosPorGrau,
    elo1Mm, elo2Mm, folgaDobra, envYMin, envRaioMin,
    s.mensagem);

  server.send(200, "application/json", json);
}

// ---------------------------------------------------------------------
// Caminho gravado, reamostrado para caber na resposta.
// ---------------------------------------------------------------------
static void handleTrajetoria() {
  registrarContatoWeb();

  Snapshot s;
  lerSnapshot(s);
  if (s.modo == MODO_GRAVANDO) {   // buffer sendo escrito pelo core 1
    server.send(200, "application/json", "{\"pts\":[]}");
    return;
  }

  const uint16_t n = trajPontos();
  const Waypoint* wp = trajBuffer();

  const uint16_t MAX_SAIDA = 180;
  const uint16_t passo = (n > MAX_SAIDA) ? (n / MAX_SAIDA + 1) : 1;

  String out;
  out.reserve(3072);
  out += "{\"pts\":[";
  bool primeiro = true;
  for (uint16_t i = 0; i < n; i += passo) {
    const float a1 = passosParaGraus(J1, wp[i].p1);
    const float a2 = passosParaGraus(J2, wp[i].p2);
    float xc, yc, xp, yp;
    cinematicaDireta(a1, a2, xc, yc, xp, yp);

    if (!primeiro) out += ',';
    primeiro = false;
    out += '[';
    out += String(xp, 1); out += ',';
    out += String(yp, 1); out += ',';
    out += (int)wp[i].solda;
    out += ']';
  }
  out += "]}";
  server.send(200, "application/json", out);
}

// Lista de pontos do programa, ja convertida para graus e mm.
static void handlePontos() {
  registrarContatoWeb();
  const uint8_t n = progQuantidade();
  const Ponto* lista = progLista();

  String out;
  out.reserve(2048);
  out += "{\"pts\":[";
  for (uint8_t i = 0; i < n; i++) {
    const float a1 = passosParaGraus(J1, lista[i].p1);
    const float a2 = passosParaGraus(J2, lista[i].p2);
    float xc, yc, xp, yp;
    cinematicaDireta(a1, a2, xc, yc, xp, yp);
    if (i) out += ',';
    out += "{\"t1\":"; out += String(a1, 1);
    out += ",\"t2\":"; out += String(a2, 1);
    out += ",\"x\":";  out += String(xp, 0);
    out += ",\"y\":";  out += String(yp, 0);
    out += ",\"s\":";  out += (int)lista[i].soldaAteProximo;
    out += '}';
  }
  out += "]}";
  server.send(200, "application/json", out);
}

// ---------------------------------------------------------------------
// Comandos
// ---------------------------------------------------------------------
static void handleJog() {
  registrarContatoWeb();
  const long j = argL("j", 0);
  const long d = argL("d", 0);
  if (j != 1 && j != 2) { erro("junta invalida"); return; }
  enfileirar(CMD_JOG, (int32_t)j, (int32_t)(d > 0 ? 1 : (d < 0 ? -1 : 0)));
}

// A PARADA nao entra na fila: escreve direto a flag que o loop() testa no
// primeiro instante do ciclo, antes de drenar a fila de comandos.
static void handleParar() {
  registrarContatoWeb();
  solicitarParada();
  ok();
}

static void handlePrecisao()   { registrarContatoWeb(); enfileirar(CMD_PRECISAO, argL("v", -1)); }
static void handleServos()     { registrarContatoWeb(); enfileirar(CMD_SERVOS, argL("v", 0)); }
static void handleSolda()      { registrarContatoWeb(); enfileirar(CMD_SOLDA, argL("v", 0)); }
static void handleTesteRele()  { registrarContatoWeb(); enfileirar(CMD_TESTE_RELE); }
static void handlePontoGravar(){ registrarContatoWeb(); enfileirar(CMD_PONTO_GRAVAR); }
static void handlePontoRemover(){registrarContatoWeb(); enfileirar(CMD_PONTO_REMOVER, argL("i",-1)); }
static void handlePontoSolda() { registrarContatoWeb(); enfileirar(CMD_PONTO_SOLDA, argL("i",-1), argL("v",0)); }
static void handleProgLimpar() { registrarContatoWeb(); enfileirar(CMD_PROG_LIMPAR); }
static void handleProgParar()  { registrarContatoWeb(); enfileirar(CMD_PROG_PARAR); }
static void handleIrPonto()    { registrarContatoWeb(); enfileirar(CMD_IR_PARA_PONTO, argL("i",-1)); }
static void handleProgExec()   { registrarContatoWeb(); enfileirar(CMD_PROG_EXECUTAR, argL("ensaio",1)); }
static void handleGravarIni()  { registrarContatoWeb(); enfileirar(CMD_GRAVAR_INICIAR); }
static void handleGravarFim()  { registrarContatoWeb(); enfileirar(CMD_GRAVAR_PARAR); }
static void handleReproduzir() { registrarContatoWeb(); enfileirar(CMD_REPRODUZIR); }
static void handleTrajLimpar() { registrarContatoWeb(); enfileirar(CMD_TRAJ_LIMPAR); }
static void handleHome()       { registrarContatoWeb(); enfileirar(CMD_IR_HOME); }
static void handleCalibIni()   { registrarContatoWeb(); enfileirar(CMD_CALIB_INICIAR); }
static void handleCalibConf()  { registrarContatoWeb(); enfileirar(CMD_CALIB_CONFIRMAR); }
static void handleCalibCanc()  { registrarContatoWeb(); enfileirar(CMD_CALIB_CANCELAR); }

static void handleMover() {
  registrarContatoWeb();
  Snapshot s;
  lerSnapshot(s);
  const float t1 = argF("t1", s.t1);
  const float t2 = argF("t2", s.t2);
  const char* motivo = nullptr;
  if (!posturaValida(t1, t2, &motivo)) { erro(motivo ? motivo : "postura invalida"); return; }
  enfileirar(CMD_MOVER_ANGULOS, 0, 0, t1, t2);
}

static void handleMoverXY() {
  registrarContatoWeb();
  if (!server.hasArg("x") || !server.hasArg("y")) { erro("faltam x e y"); return; }

  Snapshot s;
  lerSnapshot(s);
  const float x = argF("x", 0);
  const float y = argF("y", 0);

  float t1, t2;
  const char* motivo = nullptr;
  // A cinematica inversa e calculo puro: pode rodar aqui sem tocar nos
  // motores. O movimento em si continua sendo tarefa do core 1.
  if (!resolverXY(x, y, s.t1, s.t2, t1, t2, &motivo)) {
    erro(motivo ? motivo : "ponto invalido");
    return;
  }
  enfileirar(CMD_MOVER_ANGULOS, 0, 0, t1, t2);
}

// ---------------------------------------------------------------------
// Configuracao
// ---------------------------------------------------------------------
// Os tres handlers abaixo NAO escrevem nas variaveis vivas. Eles validam
// os argumentos, preenchem a area de preparo (estado.h) e enfileiram
// CMD_APLICAR_CONFIG. Quem copia para o estado vivo e chama
// recalcularResolucao() e o core 1, num ponto seguro do ciclo e so com o
// robo em modo manual.
//
// A versao anterior escrevia direto daqui, do core 0: recalcularResolucao()
// altera passosPorGrau, grausMin e grausMax enquanto jogAtualizar() os le.
static bool exigirManual() {
  Snapshot s;
  lerSnapshot(s);
  if (s.modo != MODO_MANUAL) {
    erro("ajuste so com o robo parado no modo manual");
    return false;
  }
  return true;
}

static void handleConfig() {
  registrarContatoWeb();
  if (!exigirManual()) return;

  const long vn = argL("velN",  velNormal);
  const long vp = argL("velP",  velPrecisao);
  const long va = argL("velA",  velAuto);
  const float vs = argF("velCordao", velCordaoMmS);
  const float vc = argF("velC", velCordaoMmS);
  const long a1 = argL("acel1", J1.aceleracao);
  const long a2 = argL("acel2", J2.aceleracao);
  const long  pv1 = argL("ppv1", J1.passosPorVolta);
  const float rd1 = argF("red1", J1.reducao);
  const long  pv2 = argL("ppv2", J2.passosPorVolta);
  const float rd2 = argF("red2", J2.reducao);
  const long es = argL("escala", escalaVelocidadeTraj);

  if (vn <= 0 || vp <= 0 || va <= 0 || vs <= 0 || a1 <= 0 || a2 <= 0 || pv1 <= 0 || rd1 <= 0 || pv2 <= 0 || rd2 <= 0) {
    erro("valor invalido"); return;
  }
  if ((uint32_t)vn > FREQ_PULSO_MAX_HZ || (uint32_t)vp > FREQ_PULSO_MAX_HZ ||
      (uint32_t)va > FREQ_PULSO_MAX_HZ) {
    erro("velocidade acima do limite do driver"); return;
  }

  prepararConfigPendente();
  configPendente.velNormal    = (uint32_t)vn;
  configPendente.velPrecisao  = (uint32_t)vp;
  configPendente.velAuto      = (uint32_t)va;
  configPendente.velCordaoMmS = (vc > 0.05f) ? vc : vs;
  configPendente.acel1        = (uint32_t)a1;
  configPendente.acel2        = (uint32_t)a2;
  configPendente.ppv1         = (uint32_t)pv1;
  configPendente.red1         = rd1;
  configPendente.ppv2         = (uint32_t)pv2;
  configPendente.red2         = rd2;
  configPendente.escalaTraj   = (uint16_t)constrain(es, 10, 200);

  enfileirar(CMD_APLICAR_CONFIG);
}

static void handleGeometria() {
  registrarContatoWeb();
  if (!exigirManual()) return;

  const float l1 = argF("l1", elo1Mm);
  const float l2 = argF("l2", elo2Mm);
  const float db = argF("dobra", folgaDobra);
  const float ey = argF("envY", envYMin);
  const float er = argF("envR", envRaioMin);

  if (l1 <= 0 || l2 <= 0) { erro("comprimento de elo invalido"); return; }
  if (db < 0 || db > 90)  { erro("folga de dobra deve ficar entre 0 e 90"); return; }
  if (er < 0)             { erro("raio minimo invalido"); return; }

  prepararConfigPendente();
  configPendente.elo1       = l1;
  configPendente.elo2       = l2;
  configPendente.folgaDobra = db;
  configPendente.envY       = ey;
  configPendente.envRaio    = er;

  enfileirar(CMD_APLICAR_CONFIG);
}

static void handleProtecoes() {
  registrarContatoWeb();
  if (!exigirManual()) return;

  prepararConfigPendente();
  if (server.hasArg("curso"))    configPendente.protCurso    = (argL("curso", 1) != 0);
  if (server.hasArg("dobra"))    configPendente.protDobra    = (argL("dobra", 1) != 0);
  if (server.hasArg("envelope")) configPendente.protEnvelope = (argL("envelope", 0) != 0);

  enfileirar(CMD_APLICAR_CONFIG);
}

static void handleReset() {
  registrarContatoWeb();
  if (!exigirManual()) return;
  enfileirar(CMD_RESTAURAR_PADROES);
}

// ---------------------------------------------------------------------
void servidorIniciar() {
  server.on("/",                  HTTP_GET,  handleRaiz);
  server.on("/api/status",        HTTP_GET,  handleStatus);
  server.on("/api/trajetoria",    HTTP_GET,  handleTrajetoria);

  server.on("/api/jog",           HTTP_POST, handleJog);
  server.on("/api/parar",         HTTP_POST, handleParar);
  server.on("/api/precisao",      HTTP_POST, handlePrecisao);
  server.on("/api/servos",        HTTP_POST, handleServos);
  server.on("/api/solda",         HTTP_POST, handleSolda);
  server.on("/api/teste/rele",    HTTP_POST, handleTesteRele);
  server.on("/api/pontos",        HTTP_GET,  handlePontos);
  server.on("/api/ponto/gravar",  HTTP_POST, handlePontoGravar);
  server.on("/api/ponto/remover", HTTP_POST, handlePontoRemover);
  server.on("/api/ponto/solda",   HTTP_POST, handlePontoSolda);
  server.on("/api/ponto/ir",      HTTP_POST, handleIrPonto);
  server.on("/api/prog/limpar",   HTTP_POST, handleProgLimpar);
  server.on("/api/prog/executar", HTTP_POST, handleProgExec);
  server.on("/api/prog/parar",    HTTP_POST, handleProgParar);
  server.on("/api/home",          HTTP_POST, handleHome);

  server.on("/api/gravar/iniciar", HTTP_POST, handleGravarIni);
  server.on("/api/gravar/parar",   HTTP_POST, handleGravarFim);
  server.on("/api/reproduzir",     HTTP_POST, handleReproduzir);
  server.on("/api/traj/limpar",    HTTP_POST, handleTrajLimpar);

  server.on("/api/mover",         HTTP_POST, handleMover);
  server.on("/api/mover_xy",      HTTP_POST, handleMoverXY);

  server.on("/api/config",        HTTP_POST, handleConfig);
  server.on("/api/geometria",     HTTP_POST, handleGeometria);
  server.on("/api/protecoes",     HTTP_POST, handleProtecoes);
  server.on("/api/config/reset",  HTTP_POST, handleReset);

  server.on("/api/calib/iniciar",   HTTP_POST, handleCalibIni);
  server.on("/api/calib/confirmar", HTTP_POST, handleCalibConf);
  server.on("/api/calib/cancelar",  HTTP_POST, handleCalibCanc);

  server.onNotFound(handleNaoEncontrado);
  server.begin();
  Serial.println("[WEB] Servidor HTTP ouvindo na porta 80.");
}

void servidorAtender() {
  server.handleClient();
}
