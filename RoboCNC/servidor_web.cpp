#include "servidor_web.h"
#include "estado.h"
#include "cinematica.h"
#include "trajetoria.h"
#include "programa.h"
#include "armazenamento.h"
#include "calibracao.h"
#include "rede.h"
#include "pagina_web_gz.h"

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

static void enfileirarNomeado(TipoComando tipo, const char* nome) {
  if (enviarComandoNomeado(tipo, nome)) ok();
  else server.send(503, "text/plain", "fila cheia: comando nao aceito");
}

static float argF(const char* nome, float padrao) {
  return server.hasArg(nome) ? server.arg(nome).toFloat() : padrao;
}
static long argL(const char* nome, long padrao) {
  return server.hasArg(nome) ? server.arg(nome).toInt() : padrao;
}

// Pedidos que reescrevem parametro da maquina so valem com o robo
// parado. O core 1 confere de novo na hora de aplicar -- aqui a
// conferencia existe para o operador ver a recusa na tela em vez de
// apertar o botao e nada acontecer.
static bool exigirManual() {
  Snapshot s;
  lerSnapshot(s);
  if (s.modo != MODO_MANUAL) {
    erro("ajuste so com o robo parado no modo manual");
    return false;
  }
  return true;
}


// ---------------------------------------------------------------------
// A pagina vai comprimida. Alem de economizar ~53 kB de flash, ela
// chega no celular umas 3 vezes mais rapido: num ponto de acesso de
// ESP32 e a diferenca entre abrir na hora e esperar. Quem edita e
// pagina_web.h; testes/gerar_pagina_gz.py regenera o comprimido e o
// banco reprova se ele ficar velho.
static void handleRaiz() {
  registrarContatoOperador();
  Serial.printf("[WEB] Servindo pagina de controle (%u bytes comprimidos).\n",
                (unsigned)PAGINA_HTML_GZ_LEN);
  server.sendHeader("Content-Encoding", "gzip");
  server.send_P(200, "text/html",
                (PGM_P)PAGINA_HTML_GZ, PAGINA_HTML_GZ_LEN);
}

// Qualquer rota desconhecida vira log: se o navegador chegar no ESP32 e
// pedir algo inesperado, isso aparece no monitor serial em vez de sumir.
static void handleNaoEncontrado() {
  Serial.print("[WEB] Rota desconhecida: ");
  Serial.println(server.uri().c_str());
  server.send(404, "text/plain", "rota inexistente");
}

static void handleStatus() {
  registrarContatoOperador();

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
    "\"velN\":%.1f,\"velP\":%.1f,\"velA\":%.1f,\"acel1\":%.0f,\"acel2\":%.0f,"
    "\"ppv1\":%lu,\"red1\":%.3f,\"ppv2\":%lu,\"red2\":%.3f,"
    "\"inv1\":%s,\"inv2\":%s,\"suav\":%u,\"afer1\":%ld,\"afer2\":%ld,"
    "\"maxPts\":%u,"
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
    velNormal, velPrecisao, velAuto, J1.aceleracao, J2.aceleracao,
    (unsigned long)J1.passosPorVolta, J1.reducao,
    (unsigned long)J2.passosPorVolta, J2.reducao,
    J1.inverterDir ? "true" : "false", J2.inverterDir ? "true" : "false",
    (unsigned)suavidadePartida, aferirPassosDesde(1), aferirPassosDesde(2),
    (unsigned)MAX_PONTOS,
    s.v1Hz, s.v2Hz, s.vPontaMmS, J1.passosPorGrau, J2.passosPorGrau,
    elo1Mm, elo2Mm, folgaDobra, envYMin, envRaioMin,
    s.mensagem);

  server.send(200, "application/json", json);
}

// ---------------------------------------------------------------------
// Caminho gravado, reamostrado para caber na resposta.
// ---------------------------------------------------------------------
static void handleTrajetoria() {
  registrarContatoOperador();

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
  registrarContatoOperador();
  const uint8_t n = progQuantidade();
  const Ponto* lista = progLista();

  // A conferencia de cada trecho roda cinematica inversa e le os limites
  // das juntas. So e feita com o robo em MANUAL: fora disso os pontos nao
  // mudam de qualquer jeito, e nao ha por que o core 0 calcular por cima
  // de uma execucao em andamento.
  Snapshot snap;
  lerSnapshot(snap);
  const bool conferir = (snap.modo == MODO_MANUAL);

  String out;
  out.reserve(2560);
  out += "{\"conferido\":";
  out += conferir ? "true" : "false";
  out += ",\"pts\":[";
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
    if (conferir && i + 1 < n) {
      char aviso[176];
      if (!progConferirTrecho(i, aviso, sizeof(aviso))) {
        out += ",\"av\":\""; out += aviso; out += '"';
      }
    }
    out += '}';
  }
  out += "]}";
  server.send(200, "application/json", out);
}

// ---------------------------------------------------------------------
// Comandos
// ---------------------------------------------------------------------
static void handleJog() {
  registrarContatoOperador();
  const long j = argL("j", 0);
  const long d = argL("d", 0);
  if (j != 1 && j != 2) { erro("junta invalida"); return; }
  enfileirar(CMD_JOG, (int32_t)j, (int32_t)(d > 0 ? 1 : (d < 0 ? -1 : 0)));
}

// Joystick: os dois eixos numa requisicao so. Metade do trafego de
// heartbeat comparado a mandar /api/jog por eixo, o que importa num
// WebServer que atende uma conexao por vez.
static void handleJogXY() {
  registrarContatoOperador();
  const float a = argF("a", 0.0f);
  const float b = argF("b", 0.0f);
  enfileirar(CMD_JOG_XY, 0, 0,
             constrain(a, -1.0f, 1.0f), constrain(b, -1.0f, 1.0f));
}

// A PARADA nao entra na fila: escreve direto a flag que o loop() testa no
// primeiro instante do ciclo, antes de drenar a fila de comandos.
static void handleParar() {
  registrarContatoOperador();
  solicitarParada();
  ok();
}

static void handlePrecisao()   { registrarContatoOperador(); enfileirar(CMD_PRECISAO, argL("v", -1)); }
static void handleServos()     { registrarContatoOperador(); enfileirar(CMD_SERVOS, argL("v", 0)); }
static void handleSolda()      { registrarContatoOperador(); enfileirar(CMD_SOLDA, argL("v", 0)); }
static void handleTesteRele()  { registrarContatoOperador(); enfileirar(CMD_TESTE_RELE); }
static void handlePontoGravar(){ registrarContatoOperador(); enfileirar(CMD_PONTO_GRAVAR); }
static void handlePontoRemover(){registrarContatoOperador(); enfileirar(CMD_PONTO_REMOVER, argL("i",-1)); }
static void handlePontoSolda() { registrarContatoOperador(); enfileirar(CMD_PONTO_SOLDA, argL("i",-1), argL("v",0)); }
static void handleProgLimpar() { registrarContatoOperador(); enfileirar(CMD_PROG_LIMPAR); }
static void handleProgParar()  { registrarContatoOperador(); enfileirar(CMD_PROG_PARAR); }
static void handleIrPonto()    { registrarContatoOperador(); enfileirar(CMD_IR_PARA_PONTO, argL("i",-1)); }
static void handleProgExec()   { registrarContatoOperador(); enfileirar(CMD_PROG_EXECUTAR, argL("ensaio",1)); }
static void handleGravarIni()  { registrarContatoOperador(); enfileirar(CMD_GRAVAR_INICIAR); }
static void handleGravarFim()  { registrarContatoOperador(); enfileirar(CMD_GRAVAR_PARAR); }
static void handleReproduzir() { registrarContatoOperador(); enfileirar(CMD_REPRODUZIR); }
static void handleTrajLimpar() { registrarContatoOperador(); enfileirar(CMD_TRAJ_LIMPAR); }
static void handleHome()       { registrarContatoOperador(); enfileirar(CMD_IR_HOME); }
static void handleCalibIni()   { registrarContatoOperador(); enfileirar(CMD_CALIB_INICIAR); }
// g1/g2: angulo da referencia na etapa HOME, curso real medido na etapa
// de conclusao. Ausentes ou zero mantem o comportamento antigo.
static void handleCalibConf() {
  registrarContatoOperador();
  enfileirar(CMD_CALIB_CONFIRMAR, 0, 0, argF("g1", 0.0f), argF("g2", 0.0f));
}
static void handleCalibCanc()  { registrarContatoOperador(); enfileirar(CMD_CALIB_CANCELAR); }
static void handleCalibApagar(){ registrarContatoOperador(); enfileirar(CMD_CALIB_APAGAR); }
static void handleReferenciar(){
  registrarContatoOperador();
  if (!exigirManual()) return;
  enfileirar(CMD_REFERENCIAR);
}
static void handleAferirMarcar(){
  registrarContatoOperador();
  if (!exigirManual()) return;
  const long j = argL("j", 0);
  if (j != 1 && j != 2) { erro("junta invalida"); return; }
  enfileirar(CMD_AFERIR_MARCAR, j);
}
static void handleAferirAplicar(){
  registrarContatoOperador();
  if (!exigirManual()) return;
  const long  j = argL("j", 0);
  const float g = argF("g", 0.0f);
  if (j != 1 && j != 2) { erro("junta invalida"); return; }
  // Graus medidos precisam ser um angulo de verdade: dividir pulsos por
  // um numero perto de zero manda a resolucao para o infinito.
  if (!(g > 0.5f) || g > 3600.0f) {
    erro("digite quantos graus o eixo girou de verdade");
    return;
  }
  enfileirar(CMD_AFERIR_APLICAR, j, 0, g);
}

static void handleMover() {
  registrarContatoOperador();
  Snapshot s;
  lerSnapshot(s);
  const float t1 = argF("t1", s.t1);
  const float t2 = argF("t2", s.t2);
  const char* motivo = nullptr;
  if (!posturaValida(t1, t2, &motivo)) { erro(motivo ? motivo : "postura invalida"); return; }
  enfileirar(CMD_MOVER_ANGULOS, 0, 0, t1, t2);
}

static void handleMoverXY() {
  registrarContatoOperador();
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
static void handleConfig() {
  registrarContatoOperador();
  if (!exigirManual()) return;

  // Tudo em graus/s agora. Ver a nota em config.h.
  const float vn = argF("velN",  velNormal);
  const float vp = argF("velP",  velPrecisao);
  const float va = argF("velA",  velAuto);
  // "velC" e o nome antigo do mesmo parametro. Aceitar os dois e certo;
  // errado era dar prioridade ao antigo com o valor VIVO como padrao --
  // como a interface so manda "velCordao", o antigo sempre vencia com o
  // valor que ja estava, e a velocidade do cordao nunca mudava.
  float vs = velCordaoMmS;
  if      (server.hasArg("velCordao")) vs = argF("velCordao", vs);
  else if (server.hasArg("velC"))      vs = argF("velC", vs);
  const float a1 = argF("acel1", J1.aceleracao);
  const float a2 = argF("acel2", J2.aceleracao);
  const long  pv1 = argL("ppv1", J1.passosPorVolta);
  const float rd1 = argF("red1", J1.reducao);
  const long  pv2 = argL("ppv2", J2.passosPorVolta);
  const float rd2 = argF("red2", J2.reducao);
  const long es = argL("escala", escalaVelocidadeTraj);
  const long sv = argL("suav", suavidadePartida);
  const long iv1 = argL("inv1", J1.inverterDir ? 1 : 0);
  const long iv2 = argL("inv2", J2.inverterDir ? 1 : 0);

  if (vn <= 0 || vp <= 0 || va <= 0 || vs <= 0 || a1 <= 0 || a2 <= 0 || pv1 <= 0 || rd1 <= 0 || pv2 <= 0 || rd2 <= 0) {
    erro("valor invalido"); return;
  }
  // Teto em graus/s: acima disso o pulso passaria do que o driver aceita
  // na junta de maior reducao. 720 graus/s ja e o dobro de qualquer coisa
  // sensata num braco de solda.
  if (vn > 720.0f || vp > 720.0f || va > 720.0f || a1 > 5000.0f || a2 > 5000.0f) {
    erro("velocidade ou rampa fora de faixa"); return;
  }

  prepararConfigPendente();
  configPendente.velNormal    = vn;
  configPendente.velPrecisao  = vp;
  configPendente.velAuto      = va;
  configPendente.velCordaoMmS = vs;
  configPendente.acel1        = a1;
  configPendente.acel2        = a2;
  configPendente.ppv1         = (uint32_t)pv1;
  configPendente.red1         = rd1;
  configPendente.ppv2         = (uint32_t)pv2;
  configPendente.red2         = rd2;
  configPendente.escalaTraj   = (uint16_t)constrain(es, 10, 200);
  configPendente.suavidade    = (uint8_t)constrain(sv, 0, 255);
  configPendente.inv1         = (iv1 != 0);
  configPendente.inv2         = (iv2 != 0);

  enfileirar(CMD_APLICAR_CONFIG);
}

static void handleGeometria() {
  registrarContatoOperador();
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
  registrarContatoOperador();
  if (!exigirManual()) return;

  prepararConfigPendente();
  if (server.hasArg("curso"))    configPendente.protCurso    = (argL("curso", 1) != 0);
  if (server.hasArg("dobra"))    configPendente.protDobra    = (argL("dobra", 1) != 0);
  if (server.hasArg("envelope")) configPendente.protEnvelope = (argL("envelope", 0) != 0);

  enfileirar(CMD_APLICAR_CONFIG);
}

static void handleReset() {
  registrarContatoOperador();
  if (!exigirManual()) return;
  enfileirar(CMD_RESTAURAR_PADROES);
}

// ---------------------------------------------------------------------
// DESENHO NA MESA
//
// A interface manda um traco feito com o dedo sobre o desenho do braco,
// em milimetros de chapa: "x,y;x,y;...". Aqui ele vira o programa de
// pontos, e dali em diante e um programa como qualquer outro -- da para
// ensaiar, soldar, editar ponto a ponto e salvar no cartao.
//
// A cinematica inversa e calculo puro e pode rodar aqui no core 0, mas o
// programa vivo continua sendo escrito so pelo core 1. Por isso o
// caminho e exatamente o mesmo do carregamento de arquivo: preenche a
// area de troca e enfileira CMD_ARQ_APLICAR_PROG, que valida todos os
// pontos antes de trocar. Um traco que passe fora da area util nao apaga
// o programa que ja estava na maquina.
//
// A area de troca tambem e usada pela tarefa do cartao. Quem a preenche
// e sempre a tarefa do servidor (aqui ou em armSolicitar), e a tarefa do
// cartao so a le enquanto estiver OCUPADA -- recusar com armOcupado()
// fecha a janela.
static void handleProgDesenho() {
  registrarContatoOperador();
  if (!exigirManual()) return;
  if (armOcupado()) { erro("cartao ocupado: repita em um instante"); return; }

  const String corpo = server.hasArg("plain") ? server.arg("plain")
                                              : server.arg("p");
  if (corpo.length() == 0) { erro("desenho vazio"); return; }

  const bool solda = argL("solda", 0) != 0;

  Snapshot s;
  lerSnapshot(s);
  // O cotovelo nao pode trocar de lado no meio do traco: cada ponto e
  // resolvido a partir do anterior, e o primeiro a partir de onde o
  // braco esta agora. E o mesmo criterio de progAdicionarPonto.
  float refT1 = s.t1, refT2 = s.t2;

  // Estatico de proposito: a lista inteira na pilha da tarefa do servidor
  // e desnecessaria, e so existe uma tarefa de HTTP.
  static Ponto pts[MAX_PONTOS];
  uint8_t n = 0;

  const char* p = corpo.c_str();
  char* fim = nullptr;
  while (n < MAX_PONTOS) {
    while (*p == ';' || *p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++;
    if (!*p) break;

    const float x = strtof(p, &fim);
    if (fim == p) { erro("desenho mal formado"); return; }
    p = fim;
    while (*p == ' ') p++;
    if (*p != ',') { erro("desenho mal formado"); return; }
    p++;
    const float y = strtof(p, &fim);
    if (fim == p) { erro("desenho mal formado"); return; }
    p = fim;

    // Terceiro campo opcional: o arco no trecho que comeca neste ponto.
    // Um DXF traz varios contornos soltos -- solda ao longo de cada um,
    // deslocamento de um para o outro. Sem o campo, vale o ?solda= da
    // URL, que e o caso do traco a dedo.
    bool soldaAqui = solda;
    while (*p == ' ') p++;
    if (*p == ',') {
      p++;
      const long f = strtol(p, &fim, 10);
      if (fim == p) { erro("desenho mal formado"); return; }
      p = fim;
      soldaAqui = (f != 0);
    }

    float t1, t2;
    const char* motivo = nullptr;
    if (!resolverXY(x, y, refT1, refT2, t1, t2, &motivo)) {
      char m[112];
      snprintf(m, sizeof(m), "ponto %u do desenho (%.0f, %.0f mm): %s",
               (unsigned)(n + 1), x, y, motivo ? motivo : "fora de alcance");
      erro(m);
      return;
    }
    pts[n].p1 = (int32_t)grausParaPassos(J1, t1);
    pts[n].p2 = (int32_t)grausParaPassos(J2, t2);
    pts[n].soldaAteProximo = soldaAqui ? 1 : 0;
    refT1 = t1; refT2 = t2;
    n++;
  }

  while (*p == ';' || *p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++;
  if (*p) {
    char m[80];
    snprintf(m, sizeof(m), "desenho longo demais: o maximo e %u pontos",
             (unsigned)MAX_PONTOS);
    erro(m);
    return;
  }
  if (n < 2) { erro("desenho curto demais: risque um traco maior"); return; }
  // Depois do ultimo ponto nao ha trecho: deixar o arco marcado ali
  // acenderia o rele sem nada para percorrer.
  pts[n - 1].soldaAteProximo = 0;

  armStagingDefinir(pts, n);
  enfileirarNomeado(CMD_ARQ_APLICAR_PROG, "desenho");
}

// ---------------------------------------------------------------------
// REDE
//
// So leitura: a maquina tem Wi-Fi proprio e nada a configurar. O painel
// mostra por onde chegar nele, que e a unica coisa que o operador
// precisa saber.
// ---------------------------------------------------------------------
static void handleRede() {
  registrarContatoOperador();
  char json[160];
  snprintf(json, sizeof(json),
    "{\"ssid\":\"%s\",\"ip\":\"%s\",\"nome\":\"%s\"}",
    WIFI_AP_SSID, redeIpAcesso(), redeNomeLocal());
  server.send(200, "application/json", json);
}

// ---------------------------------------------------------------------
// CARTAO SD
//
// Nenhum handler faz I/O: eles consultam o estado publicado pela tarefa
// do core 0 ou enfileiram um pedido. O 'seq' na resposta muda a cada
// tarefa concluida, e e assim que a interface sabe que ha resultado novo
// sem ficar perguntando.
// ---------------------------------------------------------------------
static const char* NOMES_ARM[] = {
  "DESLIGADO", "SEM_CARTAO", "PRONTO", "OCUPADO", "ERRO"
};

static void handleSdEstado() {
  registrarContatoOperador();
  char json[320];
  const uint8_t e = (uint8_t)armEstado();
  snprintf(json, sizeof(json),
    "{\"estado\":\"%s\",\"ocupado\":%s,\"seq\":%lu,"
    "\"totalMB\":%lu,\"livreMB\":%lu,\"msg\":\"%s\"}",
    (e < 5) ? NOMES_ARM[e] : "?",
    armOcupado() ? "true" : "false",
    (unsigned long)armSequencia(),
    (unsigned long)(armBytesTotais() / (1024ULL * 1024ULL)),
    (unsigned long)(armBytesLivres() / (1024ULL * 1024ULL)),
    armMensagem());
  server.send(200, "application/json", json);
}

static void handleSdLista() {
  registrarContatoOperador();
  const ArmTipo t = armTipoDe(server.arg("tipo").c_str());
  if (t == TIPO_INVALIDO) { erro("tipo invalido"); return; }

  // A listagem publicada pode ser de outra pasta: pede a atualizacao e
  // devolve o que ha agora. A interface recarrega quando 'seq' mudar.
  if (armListaTipo() != t && !armOcupado()) armSolicitar(TAR_LISTAR, server.arg("tipo").c_str());

  String out;
  out.reserve(1024);
  out += "{\"tipo\":\"";
  out += server.arg("tipo");
  out += "\",\"pronto\":";
  out += (armListaTipo() == t) ? "true" : "false";
  out += ",\"arq\":[";
  if (armListaTipo() == t) {
    const ArmEntrada* l = armLista();
    for (uint8_t i = 0; i < armListaN(); i++) {
      if (i) out += ',';
      out += "{\"n\":\""; out += l[i].nome;
      out += "\",\"b\":";  out += String((int)l[i].bytes);
      out += '}';
    }
  }
  out += "]}";
  server.send(200, "application/json", out);
}

static void handleSdMontar() {
  registrarContatoOperador();
  if (!armSolicitar(TAR_MONTAR, "")) { erro("cartao ocupado"); return; }
  ok();
}

static void handleSdApagar() {
  registrarContatoOperador();
  const String tipo = server.arg("tipo");
  const String nome = server.arg("nome");
  if (armTipoDe(tipo.c_str()) == TIPO_INVALIDO) { erro("tipo invalido"); return; }
  if (!armNomeValido(nome.c_str()))             { erro("nome invalido"); return; }
  char alvo[48];
  snprintf(alvo, sizeof(alvo), "%s/%s", tipo.c_str(), nome.c_str());
  if (!armSolicitar(TAR_APAGAR, alvo)) { erro("cartao ocupado ou ausente"); return; }
  ok();
}

// Salvar passa pelo core 1 (ele prepara a area de troca); carregar de
// programa e configuracao vai direto para a tarefa de SD, que so avisa o
// core 1 depois de ler e validar o arquivo. Trajetoria e a excecao: o
// core 1 precisa emprestar o buffer antes.
static void handleSdSalvar() {
  registrarContatoOperador();
  const String tipo = server.arg("tipo");
  const String nome = server.arg("nome");
  if (!armNomeValido(nome.c_str())) {
    erro("nome invalido: use letras, numeros, espaco, - e _");
    return;
  }
  switch (armTipoDe(tipo.c_str())) {
    case TIPO_PROG: enfileirarNomeado(CMD_ARQ_SALVAR_PROG,   nome.c_str()); return;
    case TIPO_TRAJ: enfileirarNomeado(CMD_ARQ_SALVAR_TRAJ,   nome.c_str()); return;
    case TIPO_CFG:  enfileirarNomeado(CMD_ARQ_SALVAR_CONFIG, nome.c_str()); return;
    default:        erro("tipo invalido"); return;
  }
}

static void handleSdCarregar() {
  registrarContatoOperador();
  const String tipo = server.arg("tipo");
  const String nome = server.arg("nome");
  if (!armNomeValido(nome.c_str())) { erro("nome invalido"); return; }

  const ArmTipo t = armTipoDe(tipo.c_str());
  if (t == TIPO_TRAJ) {
    // Precisa do emprestimo do buffer, entao passa pelo core 1.
    enfileirarNomeado(CMD_ARQ_CARREGAR_TRAJ, nome.c_str());
    return;
  }
  const ArmTarefa tarefa = (t == TIPO_PROG) ? TAR_CARREGAR_PROG
                         : (t == TIPO_CFG)  ? TAR_CARREGAR_CONFIG
                                            : TAR_NENHUMA;
  if (tarefa == TAR_NENHUMA) { erro("tipo invalido"); return; }
  if (!armSolicitar(tarefa, nome.c_str())) { erro("cartao ocupado ou ausente"); return; }
  ok();
}

// ---------------------------------------------------------------------
// Manifesto: e o que faz o navegador do celular tratar a pagina como
// aplicativo (tela cheia, sem barra de endereco) quando o operador usa
// "adicionar a tela inicial". Icone embutido em SVG para nao depender de
// arquivo nenhum - o ponto de acesso nao tem internet.
// ---------------------------------------------------------------------
static const char MANIFESTO[] PROGMEM =
  "{\"name\":\"RoboCNC 2DOF\",\"short_name\":\"RoboCNC\","
  "\"start_url\":\"/\",\"display\":\"standalone\",\"orientation\":\"any\","
  "\"background_color\":\"#0f1216\",\"theme_color\":\"#0f1216\","
  "\"icons\":[{\"src\":\"/icone.svg\",\"sizes\":\"any\",\"type\":\"image/svg+xml\","
  "\"purpose\":\"any\"}]}";

static const char ICONE[] PROGMEM =
  "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 192 192'>"
  "<rect width='192' height='192' rx='34' fill='#0f1216'/>"
  "<g fill='none' stroke='#ff6a2b' stroke-width='13' stroke-linecap='round'>"
  "<path d='M46 140 L96 74 L150 96'/></g>"
  "<circle cx='46' cy='140' r='13' fill='#7d9dff'/>"
  "<circle cx='96' cy='74' r='9' fill='#7d9dff'/>"
  "<circle cx='150' cy='96' r='7' fill='#ff3b1f'/></svg>";

static void handleManifesto() {
  server.send_P(200, "application/manifest+json", MANIFESTO);
}
static void handleIcone() {
  server.send_P(200, "image/svg+xml", ICONE);
}

// ---------------------------------------------------------------------
void servidorIniciar() {
  server.on("/",                  HTTP_GET,  handleRaiz);
  server.on("/api/status",        HTTP_GET,  handleStatus);
  server.on("/api/trajetoria",    HTTP_GET,  handleTrajetoria);

  server.on("/api/jog",           HTTP_POST, handleJog);
  server.on("/api/jogxy",         HTTP_POST, handleJogXY);
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
  server.on("/api/prog/desenho",  HTTP_POST, handleProgDesenho);
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

  server.on("/api/sd",            HTTP_GET,  handleSdEstado);
  server.on("/api/sd/lista",      HTTP_GET,  handleSdLista);
  server.on("/api/sd/salvar",     HTTP_POST, handleSdSalvar);
  server.on("/api/sd/carregar",   HTTP_POST, handleSdCarregar);
  server.on("/api/sd/apagar",     HTTP_POST, handleSdApagar);
  server.on("/api/sd/montar",     HTTP_POST, handleSdMontar);

  server.on("/manifest.webmanifest", HTTP_GET, handleManifesto);
  server.on("/icone.svg",            HTTP_GET, handleIcone);

  server.on("/api/calib/iniciar",   HTTP_POST, handleCalibIni);
  server.on("/api/calib/confirmar", HTTP_POST, handleCalibConf);
  server.on("/api/calib/cancelar",  HTTP_POST, handleCalibCanc);
  server.on("/api/calib/apagar",    HTTP_POST, handleCalibApagar);
  server.on("/api/referenciar",     HTTP_POST, handleReferenciar);
  server.on("/api/aferir/marcar",   HTTP_POST, handleAferirMarcar);
  server.on("/api/aferir/aplicar",  HTTP_POST, handleAferirAplicar);

  server.on("/api/rede",           HTTP_GET,  handleRede);

  server.onNotFound(handleNaoEncontrado);
  server.begin();
  Serial.println("[WEB] Servidor HTTP ouvindo na porta 80.");
}

void servidorAtender() {
  server.handleClient();
}
