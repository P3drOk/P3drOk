#include "servidor_web.h"
#include "estado.h"
#include "cinematica.h"
#include "trajetoria.h"
#include "programa.h"
#include "armazenamento.h"
#include "calibracao.h"
#include "rede.h"
#include "encoder.h"
#include "correcao.h"
#include "aprender.h"
#include "ota.h"
#include "pagina_web_gz.h"
#include <math.h>   // cosf/sinf/M_PI da previa de peca

static WebServer server(80);

static const char* const NOMES_MODO[] = {
  "MANUAL", "GRAVANDO", "REPRODUZINDO", "EXECUTANDO",
  "POSICIONANDO", "CALIBRANDO", "FALHA"
};
static const char* const NOMES_CALIB[] = {
  "INATIVO", "SOLTANDO", "LADO_A", "LADO_B", "RELIGANDO"
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

// ---------------------------------------------------------------------
// Texto livre indo para dentro de JSON.
//
// Varias mensagens da maquina trazem aspas -- `programa "peca 1" salvo`
// e a mais comum de todas, sai toda vez que alguem grava no cartao. Sem
// escapar, a resposta vira:
//
//     {"msg":"programa "peca 1" salvo"}
//
// que NAO e JSON. O r.json() do navegador lanca excecao, o contador de
// quedas sobe, e a interface anuncia "sem comunicacao" com a maquina
// funcionando perfeitamente. Repare que o numero de aspas fica PAR:
// nenhuma conferencia frouxa pega isso, so um analisador de verdade.
//
// Escapar e trabalho de quem escreve o JSON, nao de quem escreve a
// mensagem: nenhum modulo do firmware deveria precisar saber que o texto
// dele um dia vai viajar dentro de aspas.
// ---------------------------------------------------------------------
static void jsonTexto(char* destino, size_t tam, const char* origem) {
  if (!destino || tam == 0) return;
  size_t k = 0;
  for (size_t i = 0; origem && origem[i] && k + 2 < tam; i++) {
    const unsigned char c = (unsigned char)origem[i];
    if (c == '"' || c == '\\') { destino[k++] = '\\'; destino[k++] = (char)c; }
    else if (c == '\n')          { destino[k++] = '\\'; destino[k++] = 'n'; }
    else if (c == '\r')          { destino[k++] = '\\'; destino[k++] = 'r'; }
    else if (c == '\t')          { destino[k++] = '\\'; destino[k++] = 't'; }
    else if (c < 0x20)           { continue; }   // controle cru nao tem lugar
    else                         { destino[k++] = (char)c; }
  }
  destino[k] = '\0';
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

// Windows, Android e iPhone testam a rede assim que entram nela: pedem um
// endereco conhecido e esperam uma resposta especifica. Como a maquina
// responde a QUALQUER nome (o DNS de captura), essas perguntas caem aqui.
//
// Responder 404 e o pior dos mundos: o sistema conclui "esta rede nao tem
// internet", fica repetindo a pergunta de segundos em segundos -- foi a
// enxurrada de "/connecttest.txt" no monitor serial -- e, no Windows,
// chega a largar a rede sozinho.
//
// Um redirecionamento resolve os dois lados: o sistema entende que e uma
// rede com portal, e ABRE O PAINEL sozinho na tela do operador. Ele nao
// precisa nem saber o que e 192.168.4.1.
static bool ehTesteDeRede(const char* u) {
  static const char* const SONDAS[] = {
    "/connecttest.txt", "/ncsi.txt", "/fwlink",              // Windows
    "/generate_204", "/gen_204",                             // Android
    "/hotspot-detect.html", "/library/test/success.html",    // iOS
    "/success.txt", "/canonical.html",                       // Firefox
    "/redirect", "/kindle-wifi/wifistub.html",               // outros
  };
  for (const char* p : SONDAS) if (strcmp(u, p) == 0) return true;
  return false;
}

static void handleNaoEncontrado() {
  const String uri = server.uri();
  const char*  u   = uri.c_str();

  if (ehTesteDeRede(u)) {
    // O destino e o IP, nao o nome: "robo2dof.local" depende de mDNS, que
    // o Windows so resolve com Bonjour instalado. O IP sempre funciona, e
    // e justamente o Windows que mais insiste nesta pergunta.
    char destino[40];
    snprintf(destino, sizeof(destino), "http://%u.%u.%u.%u/",
             (unsigned)WIFI_AP_IP[0], (unsigned)WIFI_AP_IP[1],
             (unsigned)WIFI_AP_IP[2], (unsigned)WIFI_AP_IP[3]);
    server.sendHeader("Location", destino);
    // Sem log: e justamente a pergunta que se repete sozinha, e a
    // enxurrada esconde no monitor serial o que importa.
    server.send(302, "text/plain", "");
    return;
  }

  Serial.print("[WEB] Rota desconhecida: ");
  Serial.println(u);
  server.send(404, "text/plain", "rota inexistente");
}

static void handleStatus() {
  registrarContatoOperador();

  Snapshot s;
  lerSnapshot(s);

  const char* modo  = (s.modo  < 7)  ? NOMES_MODO[s.modo]   : "?";
  const char* calib = (s.calib < 6) ? NOMES_CALIB[s.calib] : "?";

  // Os dois eixos andam juntos na calibracao: 3 quer dizer "os dois".
  const uint8_t eixoCalib = (s.calib == CAL_INATIVO) ? 0 : 3;

  // Estado do aprendizado vai no status, e nao numa rota propria: a
  // tela precisa saber que o braco esta solto TODO ciclo, e nao so
  // quando alguem abre o painel do encoder.
  const ResumoAprender ra = aprenderResumo();
  const LeituraEncoder lidoJ1 = encoderLer(1);
  const LeituraEncoder lidoJ2 = encoderLer(2);

  // Dobro do tamanho da mensagem: no pior caso todo caractere vira dois.
  char msgSegura[sizeof(s.mensagem) * 2];
  jsonTexto(msgSegura, sizeof(msgSegura), s.mensagem);

  char json[1520];
  snprintf(json, sizeof(json),
    "{\"modo\":\"%s\",\"calib\":\"%s\",\"calibEixo\":%u,"
    "\"p1\":%ld,\"p2\":%ld,\"t1\":%.2f,\"t2\":%.2f,\"x\":%.1f,\"y\":%.1f,"
    "\"solda\":%s,\"servos\":%s,\"movendo\":%s,"
    "\"cal1\":%s,\"cal2\":%s,"
    "\"j1min\":%.1f,\"j1max\":%.1f,\"j2min\":%.1f,\"j2max\":%.1f,"
    "\"trajN\":%u,\"trajMs\":%lu,\"trajPct\":%u,\"escala\":%u,"
    "\"progN\":%u,\"progIdx\":%u,\"progPct\":%u,\"ensaio\":%s,\"velCordao\":%.1f,"
    "\"velC\":%.1f,\"protCurso\":%s,\"protDobra\":%s,\"protEnv\":%s,"
    "\"velN\":%.1f,\"velA\":%.1f,\"velMn\":%.1f,\"velMx\":%.1f,"
    "\"acel1\":%.0f,\"acel2\":%.0f,"
    "\"ppv1\":%lu,\"red1\":%.3f,\"ppv2\":%lu,\"red2\":%.3f,"
    "\"ppvM1\":%lu,\"ppvM2\":%lu,"
    "\"fatR1\":%.3f,\"fatR2\":%.3f,"
    "\"inv1\":%s,\"inv2\":%s,\"suav\":%u,"
    "\"maxPts\":%u,"
    "\"v1\":%.0f,\"v2\":%.0f,\"vPonta\":%.1f,\"ppg1\":%.2f,\"ppg2\":%.2f,"
    "\"l1\":%.1f,\"l2\":%.1f,\"dobra\":%.1f,\"envY\":%.1f,\"envR\":%.1f,"
    "\"aprBotao\":%s,\"apr\":%s,\"aprSolto\":%s,\"aprN\":%u,"
    "\"pausa\":%s,\"desf\":%s,\"ciclos\":%lu,\"cicSes\":%lu,"
    "\"m1\":%.2f,\"m2\":%.2f,\"m1ok\":%s,\"m2ok\":%s,\"trecho\":%u,"
    "\"mesaOn\":%s,\"mesaX0\":%.0f,\"mesaX1\":%.0f,\"mesaY0\":%.0f,\"mesaY1\":%.0f,"
    "\"sonReg\":%u,\"sonL\":%u,\"sonD\":%u,\"sonF16\":%s,\"sonEst\":%u,"
    "\"srv1\":%s,\"srv2\":%s,"
    "\"fvel1\":%.2f,\"fvel2\":%.2f,\"cg1\":%.3f,\"cg2\":%.3f,"
    "\"msg\":\"%s\"}",
    modo, calib, (unsigned)eixoCalib,
    s.p1, s.p2, s.t1, s.t2, s.x, s.y,
    s.solda ? "true" : "false",
    s.servosLigados ? "true" : "false",
    s.emMovimento ? "true" : "false",
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
    velNormal, velAuto, velMinima, velMaxima,
    J1.aceleracao, J2.aceleracao,
    (unsigned long)J1.passosPorVolta, J1.reducao,
    (unsigned long)J2.passosPorVolta, J2.reducao,
    // O que o encoder MEDIU de pulsos por volta. Sugestao, nao regua:
    // a tela mostra ao lado do campo e quem decide e uma pessoa.
    (unsigned long)J1.ppvMedido, (unsigned long)J2.ppvMedido,
    // O fator aprendido nas viagens: 1,0 = a regua digitada bate com o
    // que o encoder mediu andando; 0,25 = a regua esta quatro vezes
    // maior e o firmware esta segurando o eixo nessa proporcao.
    (double)correcaoFatorRegua(1), (double)correcaoFatorRegua(2),
    J1.inverterDir ? "true" : "false", J2.inverterDir ? "true" : "false",
    (unsigned)suavidadePartida,
    (unsigned)MAX_PONTOS,
    s.v1Hz, s.v2Hz, s.vPontaMmS, J1.passosPorGrau, J2.passosPorGrau,
    elo1Mm, elo2Mm, folgaDobra, envYMin, envRaioMin,
    ra.instalado ? "true" : "false", ra.ativo ? "true" : "false",
    ra.bracoSolto ? "true" : "false", (unsigned)ra.gravados,
    progPausado() ? "true" : "false",
    progTemDesfazer() ? "true" : "false",
    (unsigned long)producao.ciclosTotais, (unsigned long)producao.ciclosSessao,
    // Angulo MEDIDO pelo encoder, ao lado do comandado. E a leitura que o
    // operador quer de relance: onde o braco esta de verdade, e nao onde
    // a contagem de pulsos acha que ele esta.
    lidoJ1.graus, lidoJ2.graus,
    // Nao basta ser recente: tem de ser possivel. Sem isto a regua
    // mostrava "177667 graus medido" com ar de leitura boa.
    leituraConfiavel(1) ? "true" : "false",
    leituraConfiavel(2) ? "true" : "false",
    (unsigned)progFracaoTrecho(),
    areaMesa.definida ? "true" : "false",
    areaMesa.xMin, areaMesa.xMax, areaMesa.yMin, areaMesa.yMax,
    (unsigned)configSon.reg, (unsigned)configSon.valLiga,
    (unsigned)configSon.valDesliga, configSon.funcao16 ? "true" : "false",
    (unsigned)encoderSonEstado(),
    J1.habilitado ? "true" : "false", J2.habilitado ? "true" : "false",
    (double)J1.fatorVel, (double)J2.fatorVel,
    (double)configEncoder.contagensPorGrau[0],
    (double)configEncoder.contagensPorGrau[1],
    msgSegura);

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
        char avSeguro[sizeof(aviso) * 2];
        jsonTexto(avSeguro, sizeof(avSeguro), aviso);
        out += ",\"av\":\""; out += avSeguro; out += '"';
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

// v = 0|1 (desliga/liga), j = 1|2|0 (junta, 0 = as duas).
static void handleServos()     { registrarContatoOperador();
                                 enfileirar(CMD_SERVOS, argL("v", 0), argL("j", 0)); }
static void handleSolda()      { registrarContatoOperador(); enfileirar(CMD_SOLDA, argL("v", 0)); }
static void handleTesteRele()  { registrarContatoOperador(); enfileirar(CMD_TESTE_RELE); }
static void handlePontoGravar(){ registrarContatoOperador(); enfileirar(CMD_PONTO_GRAVAR); }
static void handlePontoRemover(){registrarContatoOperador(); enfileirar(CMD_PONTO_REMOVER, argL("i",-1)); }
static void handlePontoSolda() { registrarContatoOperador(); enfileirar(CMD_PONTO_SOLDA, argL("i",-1), argL("v",0)); }
static void handleProgLimpar() { registrarContatoOperador(); enfileirar(CMD_PROG_LIMPAR); }
static void handleProgParar()  { registrarContatoOperador(); enfileirar(CMD_PROG_PARAR); }
static void handleIrPonto()    { registrarContatoOperador(); enfileirar(CMD_IR_PARA_PONTO, argL("i",-1)); }
// Abrir arco exige confirmacao EXPLICITA na requisicao, nao so na tela.
// A tela ja pede dois toques, mas a trava tem de existir tambem aqui: a
// rota e alcancavel por qualquer coisa na rede da maquina, e "executar
// com arco" e a unica acao deste sistema que queima material e pode pegar
// fogo. Ensaio nao precisa -- ele existe justamente para ser barato.
static void handleProgExec() {
  registrarContatoOperador();
  const bool ensaio = argL("ensaio", 1) != 0;
  if (!ensaio && argL("conf", 0) != 1) {
    erro("execucao com arco exige confirmacao (conf=1)");
    return;
  }
  enfileirar(CMD_PROG_EXECUTAR, ensaio ? 1 : 0);
}

static void handleProgPausar() {
  registrarContatoOperador();
  enfileirar(CMD_PROG_PAUSAR, argL("on", -1));
}
static void handleProgDesfazer() {
  registrarContatoOperador();
  if (!exigirManual()) return;
  enfileirar(CMD_PROG_DESFAZER);
}
static void handleProgRepetir() {
  registrarContatoOperador();
  if (argL("conf", 0) != 1) {
    erro("repetir abre o arco: exige confirmacao (conf=1)");
    return;
  }
  enfileirar(CMD_PROG_REPETIR);
}
static void handleMesaCanto() {
  registrarContatoOperador();
  if (!exigirManual()) return;
  enfileirar(CMD_MESA_CANTO);
}
static void handleMesaLimpar() {
  registrarContatoOperador();
  if (!exigirManual()) return;
  enfileirar(CMD_MESA_LIMPAR);
}

// Estado da calibracao, para a aba propria dela. Junta num lugar so o que
// estava espalhado por tres rotas: resolucao medida, marca de afericao,
// voltas do motor acumuladas e a area da mesa.
static void handleCalibracao() {
  registrarContatoOperador();
  char json[640];
  snprintf(json, sizeof(json),
    "{\"cal1\":%s,\"cal2\":%s,"
    "\"ppv1\":%lu,\"ppv2\":%lu,\"red1\":%.4f,\"red2\":%.4f,"
    "\"ppg1\":%.3f,\"ppg2\":%.3f,"
    "\"g1min\":%.2f,\"g1max\":%.2f,\"g2min\":%.2f,\"g2max\":%.2f,"
    "\"enc1\":%s,\"enc2\":%s,"
    "\"mesaOn\":%s,\"mesaN\":%u,\"mesaX0\":%.1f,\"mesaX1\":%.1f,"
    "\"mesaY0\":%.1f,\"mesaY1\":%.1f,"
    "\"envY\":%.1f,\"envR\":%.1f}",
    J1.calibrada ? "true" : "false", J2.calibrada ? "true" : "false",
    (unsigned long)J1.passosPorVolta, (unsigned long)J2.passosPorVolta,
    J1.reducao, J2.reducao, J1.passosPorGrau, J2.passosPorGrau,
    J1.grausMin, J1.grausMax, J2.grausMin, J2.grausMax,
    leituraConfiavel(1) ? "true" : "false",
    leituraConfiavel(2) ? "true" : "false",
    areaMesa.definida ? "true" : "false", (unsigned)areaMesa.cantos,
    areaMesa.xMin, areaMesa.xMax, areaMesa.yMin, areaMesa.yMax,
    envYMin, envRaioMin);
  server.send(200, "application/json", json);
}

static void handleManutencaoOk() {
  registrarContatoOperador();
  if (!exigirManual()) return;
  enfileirar(CMD_MANUTENCAO_OK);
}
static void handleGravarIni()  { registrarContatoOperador(); enfileirar(CMD_GRAVAR_INICIAR); }
static void handleGravarFim()  { registrarContatoOperador(); enfileirar(CMD_GRAVAR_PARAR); }
static void handleReproduzir() { registrarContatoOperador(); enfileirar(CMD_REPRODUZIR); }
static void handleTrajLimpar() { registrarContatoOperador(); enfileirar(CMD_TRAJ_LIMPAR); }
static void handleHome()       { registrarContatoOperador(); enfileirar(CMD_IR_HOME); }
static void handleCalibIni()   { registrarContatoOperador(); enfileirar(CMD_CALIB_INICIAR); }
// Marcar a etapa atual. A calibracao nao pergunta nada: g1/g2 ficaram
// aceitos e ignorados para uma tela velha em cache nao virar erro.
static void handleCalibConf() {
  registrarContatoOperador();
  enfileirar(CMD_CALIB_CONFIRMAR);
}
static void handleCalibCanc()  { registrarContatoOperador(); enfileirar(CMD_CALIB_CANCELAR); }
static void handleCalibApagar(){ registrarContatoOperador(); enfileirar(CMD_CALIB_APAGAR); }
static void handleReferenciar(){
  registrarContatoOperador();
  if (!exigirManual()) return;
  enfileirar(CMD_REFERENCIAR);
}
// Sentido do eixo. Nao passa por exigirManual(): a etapa de referencia
// da calibracao tambem aceita, porque e exatamente ali que o operador
// descobre que o braco gira para o outro lado. Quem decide de verdade e
// o core 1, que enxerga a etapa do assistente.
static void handleSentido() {
  registrarContatoOperador();
  const long j = argL("j", 0);
  if (j != 1 && j != 2) { erro("junta invalida"); return; }

  Snapshot s;
  lerSnapshot(s);
  if (s.modo != MODO_MANUAL &&
      !(s.modo == MODO_CALIBRANDO && s.calib == CAL_LADO_A)) {
    erro("troque o sentido com o robo em manual, ou na primeira etapa da calibracao");
    return;
  }
  enfileirar(CMD_INVERTER_EIXO, j, argL("v", 0) != 0 ? 1 : 0);
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
  const float va = argF("velA",  velAuto);
  const float vmn = argF("velMin", velMinima);
  const float vmx = argF("velMax", velMaxima);
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
  const float fv1 = argF("fvel1", J1.fatorVel);
  const float fv2 = argF("fvel2", J2.fatorVel);
  const long iv1 = argL("inv1", J1.inverterDir ? 1 : 0);
  const long iv2 = argL("inv2", J2.inverterDir ? 1 : 0);

  if (vn <= 0 || va <= 0 || vs <= 0 || a1 <= 0 || a2 <= 0 || pv1 <= 0 || rd1 <= 0 || pv2 <= 0 || rd2 <= 0) {
    erro("valor invalido"); return;
  }
  // A faixa da barra tem de ser uma faixa: minimo abaixo do maximo, e os
  // dois dentro do que o gerador de pulso aceita.
  if (vmn <= 0.0f || vmx <= 0.0f || vmn >= vmx || vmx > 720.0f) {
    erro("faixa de velocidade invalida: minimo menor que o maximo, e ate 720 graus/s");
    return;
  }
  // Teto em graus/s: acima disso o pulso passaria do que o driver aceita
  // na junta de maior reducao. 720 graus/s ja e o dobro de qualquer coisa
  // sensata num braco de solda.
  if (vn > 720.0f || va > 720.0f || a1 > 5000.0f || a2 > 5000.0f) {
    erro("velocidade ou rampa fora de faixa"); return;
  }
  // O fator multiplica a velocidade escolhida. Fora desta faixa ele
  // deixa de ser ajuste e vira outra velocidade: 0,05 e vinte vezes mais
  // devagar, 3 e o triplo -- alem disso o teto de graus/s ja nao seria
  // respeitado.
  if (fv1 < 0.05f || fv1 > 3.0f || fv2 < 0.05f || fv2 > 3.0f) {
    erro("o fator de velocidade de cada junta vai de 0,05 a 3"); return;
  }

  prepararConfigPendente();
  configPendente.velNormal    = vn;
  configPendente.velAuto      = va;
  configPendente.velMinima    = vmn;
  configPendente.velMaxima    = vmx;
  configPendente.velCordaoMmS = vs;
  configPendente.acel1        = a1;
  configPendente.acel2        = a2;
  configPendente.ppv1         = (uint32_t)pv1;
  configPendente.red1         = rd1;
  configPendente.ppv2         = (uint32_t)pv2;
  configPendente.red2         = rd2;
  configPendente.escalaTraj   = (uint16_t)constrain(es, 10, 200);
  configPendente.suavidade    = (uint8_t)constrain(sv, 0, 255);
  configPendente.fVel1        = fv1;
  configPendente.fVel2        = fv2;
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

// APAGAR TUDO. Nao e o mesmo botao que restaurar padroes, e nao pode
// parecer o mesmo: este limpa a calibracao, a mesa ensinada e o zero
// absoluto -- horas de instalacao -- e reinicia a placa.
//
// Exige uma palavra digitada, e nao so um "confirmar". Toque duplo por
// engano acontece; digitar APAGAR por engano, nao. E a mesma razao pela
// qual a rota exige o texto de novo: a tela pode ter um defeito, a porta
// nao pode confiar nela.
static void handleApagarTudo() {
  registrarContatoOperador();
  if (!exigirManual()) return;
  if (server.arg("conf") != "APAGAR") {
    erro("para apagar tudo, digite APAGAR");
    return;
  }
  Snapshot s;
  lerSnapshot(s);
  if (s.emMovimento) { erro("pare o braco antes"); return; }
  logEvento("APAGAR TUDO pedido pelo painel");
  // Responde ANTES de enfileirar: o comando reinicia a placa, e uma
  // resposta que sai depois do reinicio nao sai.
  server.send(200, "text/plain", "apagando tudo e reiniciando");
  enviarComando(CMD_APAGAR_TUDO);
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
// ENCODER
//
// A leitura roda numa tarefa do core 0; aqui so se publica o que ela
// achou e se guarda a configuracao. O endereco do registrador e
// configuravel porque o mapa Modbus do driver nao esta publicado.
// ---------------------------------------------------------------------
static void jsonEncoderJunta(String& out, uint8_t j) {
  const LeituraEncoder L = encoderLer(j);
  char b[360];
  snprintf(b, sizeof(b),
    "{\"ok\":%s,\"bruto\":%ld,\"ref\":%ld,\"graus\":%.3f,\"erro\":%.3f,"
    "\"idade\":%lu,\"n\":%lu,\"falhas\":%lu,\"saltos\":%lu,\"motivo\":%u,"
    // Derivados: calculados no firmware, com os instantes de verdade.
    "\"delta\":%ld,\"vel\":%.1f,\"rpm\":%.2f,\"sent\":%d,"
    "\"passos\":%lu,\"inv\":%lu,"
    "\"bmin\":%ld,\"bmax\":%ld,\"vmax\":%.1f,\"vmin\":%.1f}",
    L.valido ? "true" : "false", (long)L.bruto, (long)L.referencia,
    L.graus, L.erro, (unsigned long)L.idadeMs,
    (unsigned long)L.leituras, (unsigned long)L.falhas,
    (unsigned long)L.saltos, (unsigned)L.motivo,
    (long)L.delta, L.velocidade, L.rpm, (int)L.sentido,
    (unsigned long)L.passosTotais, (unsigned long)L.inversoes,
    (long)L.brutoMin, (long)L.brutoMax, L.velMax, L.velMin);
  out += b;
}


// ---------------------------------------------------------------------
// REGISTRO DE EVENTOS na tela. Sai do anel na RAM, entao funciona sem
// cartao -- que e exatamente quando ele costuma ser consultado.
// ---------------------------------------------------------------------
static void handleRegistro() {
  registrarContatoOperador();
  String out;
  out.reserve(1400);
  out += "{\"n\":";
  out += (int)logQuantosNaMemoria();
  out += ",\"linhas\":[";
  for (uint8_t i = 0; i < logQuantosNaMemoria(); i++) {
    const LinhaRegistro* l = logDaMemoria(i);
    if (!l) break;
    if (i) out += ',';
    char item[128];
    // O texto do log e nosso, mas passa por aspas de JSON: uma aspa
    // solta ali quebraria a resposta inteira e a tela diria "sem
    // comunicacao" com a maquina funcionando.
    char limpo[80];
    size_t k = 0;
    for (size_t j = 0; l->txt[j] && k < sizeof(limpo) - 1; j++) {
      const char c = l->txt[j];
      if (c == '"' || c == '\\') continue;
      if ((unsigned char)c < 0x20) continue;
      limpo[k++] = c;
    }
    limpo[k] = '\0';
    snprintf(item, sizeof(item), "{\"s\":%lu,\"t\":\"%s\"}",
             (unsigned long)(l->ms / 1000), limpo);
    out += item;
  }
  out += "]}";
  server.send(200, "application/json", out);
}

// ---------------------------------------------------------------------
// ATUALIZACAO DE FIRMWARE
//
// Duas metades: o handler de ENVIO, chamado uma vez por pedaco enquanto
// o arquivo sobe, e o de RESPOSTA, chamado no fim. Quem grava e o
// primeiro -- responder sem ele gravaria coisa nenhuma e diria "ok".
// ---------------------------------------------------------------------
static void handleOtaEnvio() {
  HTTPUpload& u = server.upload();
  if (u.status == UPLOAD_FILE_START) {
    const char* motivo = nullptr;
    if (!otaComecar(&motivo)) {
      // Nao da para responder daqui: o corpo ainda esta subindo. O
      // handler de resposta le o estado e conta o que aconteceu.
      otaCancelar(motivo);
    }
  } else if (u.status == UPLOAD_FILE_WRITE) {
    otaPedaco(u.buf, u.currentSize);
  } else if (u.status == UPLOAD_FILE_END) {
    const char* motivo = nullptr;
    otaTerminar(&motivo);
  } else if (u.status == UPLOAD_FILE_ABORTED) {
    otaCancelar("envio interrompido pelo navegador");
  }
}

static void handleOtaFim() {
  const ResumoOta o = otaResumo();
  if (o.estado == OTA_OK) {
    server.send(200, "text/plain",
                "firmware gravado; a maquina reinicia em seguida");
  } else {
    erro(o.motivo[0] ? o.motivo : "atualizacao nao concluida");
  }
}

// ---------------------------------------------------------------------
// SAUDE DA MAQUINA
//
// Uma tela so com tudo que se pergunta quando algo esta estranho: ha
// quanto tempo esta ligada, quanta memoria sobrou, quantas pecas fez,
// quanto arco ja abriu, se o encoder esta respondendo e a que taxa, se o
// cartao esta la, quantos travamentos houve.
//
// Antes disso a resposta a "esta tudo bem?" era abrir o monitor serial
// com um cabo -- que so existe na bancada, nunca na fabrica.
// ---------------------------------------------------------------------
static void handleSaude() {
  registrarContatoOperador();

  const LeituraEncoder e1 = encoderLer(1);
  const LeituraEncoder e2 = encoderLer(2);
  const Travamento     tv = correcaoTravamento();
  const ResumoAprender ra = aprenderResumo();

  const uint32_t up = millis() / 1000;
  // Taxa de acerto do barramento: e o numero que separa "o encoder nao
  // funciona" de "o encoder funciona e cai de vez em quando" -- dois
  // problemas com causas completamente diferentes.
  const uint32_t tent1 = e1.leituras + e1.falhas;
  const uint32_t tent2 = e2.leituras + e2.falhas;

  char json[1024];
  snprintf(json, sizeof(json),
    "{\"up\":%lu,\"heap\":%lu,\"heapMin\":%lu,"
    "\"flashUso\":%lu,\"flashTot\":%lu,"
    "\"ciclos\":%lu,\"ciclosSes\":%lu,\"abortados\":%lu,"
    "\"arcoS\":%lu,\"manut\":%lu,"
    "\"enc1\":{\"ok\":%lu,\"falha\":%lu,\"taxa\":%u,\"idade\":%lu,\"graus\":%.2f,\"vale\":%s},"
    "\"enc2\":{\"ok\":%lu,\"falha\":%lu,\"taxa\":%u,\"idade\":%lu,\"graus\":%.2f,\"vale\":%s},"
    "\"trav\":%lu,\"alerta\":%lu,"
    "\"cartao\":%s,\"cartaoLivre\":%lu,\"cartaoTotal\":%lu,"
    "\"apr\":%s,\"aprBotao\":%s,\"estop\":%s,\"ota\":%s,\"fw\":\"%s\"}",
    (unsigned long)up,
    (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMinFreeHeap(),
    (unsigned long)ESP.getSketchSize(),
    (unsigned long)(ESP.getSketchSize() + ESP.getFreeSketchSpace()),
    (unsigned long)producao.ciclosTotais, (unsigned long)producao.ciclosSessao,
    (unsigned long)producao.abortados,
    (unsigned long)producao.horasArcoS, (unsigned long)producao.desdeManutencao,
    (unsigned long)e1.leituras, (unsigned long)e1.falhas,
    (unsigned)(tent1 ? e1.leituras * 100 / tent1 : 0),
    (unsigned long)e1.idadeMs, e1.graus, leituraConfiavel(1) ? "true" : "false",
    (unsigned long)e2.leituras, (unsigned long)e2.falhas,
    (unsigned)(tent2 ? e2.leituras * 100 / tent2 : 0),
    (unsigned long)e2.idadeMs, e2.graus, leituraConfiavel(2) ? "true" : "false",
    (unsigned long)tv.total, (unsigned long)correcaoAlertas(),
    armBytesTotais() > 0 ? "true" : "false",
    (unsigned long)(armBytesLivres() / 1024), (unsigned long)(armBytesTotais() / 1024),
    ra.ativo ? "true" : "false", ra.instalado ? "true" : "false",
    ESTOP_FISICO_INSTALADO ? "true" : "false",
    otaDisponivel() ? "true" : "false",
    // QUAL FIRMWARE ESTA RODANDO, sem chance de mentir.
    //
    // Faltava, e a falta custou caro: um defeito ja corrigido no fonte
    // continuou aparecendo na bancada, e nao havia nada na tela que
    // dissesse se aquela placa tinha ou nao a correcao -- diagnosticar
    // o fonte enquanto a maquina roda outro binario e trabalho jogado
    // fora.
    //
    // __DATE__ nao serve: e do momento em que AQUELE arquivo foi
    // compilado, e uma correcao noutro .cpp nao mexe nele -- o carimbo
    // ficaria velho justamente na hora em que precisa estar certo. O
    // MD5 e calculado sobre o binario que esta na flash: muda quando o
    // binario muda, e so quando ele muda.
    ESP.getSketchMD5());

  server.send(200, "application/json", json);
}

static void handleEncoder() {
  registrarContatoOperador();
  Snapshot s;
  lerSnapshot(s);
  const ResumoCorrecao rc = correcaoResumo();
  const Travamento     tv = correcaoTravamento();
  const ResumoZero     rz = zeroResumo();

  char motivoCorr[sizeof(rc.motivo) * 2];
  char motivoZero[sizeof(rz.motivo) * 2];
  jsonTexto(motivoCorr, sizeof(motivoCorr), rc.motivo);
  jsonTexto(motivoZero, sizeof(motivoZero), rz.motivo);

  String out;
  out.reserve(1900);
  char cab[900];
  snprintf(cab, sizeof(cab),
    "{\"ativo\":%s,\"baud\":%lu,\"par\":%u,\"func\":%u,\"per\":%u,"
    "\"b32\":%s,\"lo\":%s,"
    "\"crOn\":%s,\"crVig\":%s,\"crTol\":%.2f,\"crMax\":%.2f,"
    "\"crAlr\":%.2f,\"crTent\":%u,"
    "\"crEst\":%u,\"crN\":%u,\"crOk\":%lu,\"crFalha\":%lu,"
    "\"crAlerta\":%lu,\"crMotivo\":\"%s\","
    "\"trvOn\":%s,\"trvJ\":%u,\"trvN\":%lu,"
    "\"zSin\":%s,\"zIr\":%s,\"zTol\":%.2f,\"zEn1\":%s,\"zEn2\":%s,"
    "\"zEst\":%u,\"zG1\":%.2f,\"zG2\":%.2f,\"zMot\":\"%s\","
    "\"id1\":%u,\"id2\":%u,\"reg1\":%u,\"reg2\":%u,"
    "\"cv1\":%.0f,\"cv2\":%.0f,\"t1\":%.3f,\"t2\":%.3f,"
    "\"j1min\":%.1f,\"j1max\":%.1f,\"j2min\":%.1f,\"j2max\":%.1f,\"j\":[",
    configEncoder.ativo ? "true" : "false",
    (unsigned long)configEncoder.baud, (unsigned)configEncoder.paridade,
    (unsigned)configEncoder.funcao, (unsigned)configEncoder.periodoMs,
    configEncoder.trintaEDois ? "true" : "false",
    configEncoder.baixaPrimeiro ? "true" : "false",
    configCorrecao.ativa  ? "true" : "false",
    configCorrecao.vigiar ? "true" : "false",
    configCorrecao.toleranciaGraus, configCorrecao.maxCorrecaoGraus,
    configCorrecao.alertaGraus, (unsigned)configCorrecao.tentativas,
    (unsigned)rc.estado, (unsigned)rc.tentativas,
    (unsigned long)rc.totalOk, (unsigned long)rc.totalDesistiu,
    (unsigned long)correcaoAlertas(), motivoCorr,
    tv.ativo ? "true" : "false", (unsigned)tv.junta, (unsigned long)tv.total,
    configZero.sincronizar ? "true" : "false",
    configZero.irParaZero  ? "true" : "false",
    configZero.toleranciaGraus,
    configZero.ensinado[0] ? "true" : "false",
    configZero.ensinado[1] ? "true" : "false",
    (unsigned)rz.estado, rz.graus[0], rz.graus[1], motivoZero,
    (unsigned)configEncoder.id[0], (unsigned)configEncoder.id[1],
    (unsigned)configEncoder.reg[0], (unsigned)configEncoder.reg[1],
    configEncoder.contagensPorVolta[0], configEncoder.contagensPorVolta[1],
    s.t1, s.t2,
    J1.grausMin, J1.grausMax, J2.grausMin, J2.grausMax);
  out += cab;
  jsonEncoderJunta(out, 1);
  out += ',';
  jsonEncoderJunta(out, 2);
  out += "]";

  // O ultimo quadro cru. Contador de falha sozinho nao diz nada: com os
  // bytes na tela da para separar "ninguem respondeu" (fio, DE/RE, id
  // errado) de "respondeu outra coisa" (funcao ou registrador errado),
  // que e a mesma pista que o codigo de teste de bancada da.
  char quadro[96];
  encoderUltimoQuadro(quadro, sizeof(quadro));
  out += ",\"quadro\":\"";
  out += quadro;   // hex e palavras fixas: nada para escapar
  out += "\"}";
  server.send(200, "application/json", out);
}

static void handleEncoderConfig() {
  registrarContatoOperador();
  if (!exigirManual()) return;

  ConfigEncoder c = configEncoder;
  c.ativo         = argL("ativo", c.ativo ? 1 : 0) != 0;
  c.baud          = (uint32_t)argL("baud", (long)c.baud);
  c.paridade      = (uint8_t) constrain(argL("par",  c.paridade), 0, 2);
  c.funcao        = (uint8_t) argL("func", c.funcao);
  c.periodoMs     = (uint16_t)constrain(argL("per", c.periodoMs), ENC_PERIODO_MIN_MS, 2000);
  c.trintaEDois   = argL("b32", c.trintaEDois ? 1 : 0) != 0;
  c.baixaPrimeiro = argL("lo",  c.baixaPrimeiro ? 1 : 0) != 0;
  c.id[0]         = (uint8_t) constrain(argL("id1",  c.id[0]), 1, 247);
  c.id[1]         = (uint8_t) constrain(argL("id2",  c.id[1]), 1, 247);
  c.reg[0]        = (uint16_t)constrain(argL("reg1", c.reg[0]), 0, 65535);
  c.reg[1]        = (uint16_t)constrain(argL("reg2", c.reg[1]), 0, 65535);
  c.contagensPorVolta[0] = argF("cv1", c.contagensPorVolta[0]);
  c.contagensPorVolta[1] = argF("cv2", c.contagensPorVolta[1]);
  // A escala ensinada tambem passa por aqui, para dar um caminho de
  // ZERAR (0 = nao ensinada, volta ao caminho antigo) sem apagar o resto.
  c.contagensPorGrau[0] = argF("cg1", c.contagensPorGrau[0]);
  c.contagensPorGrau[1] = argF("cg2", c.contagensPorGrau[1]);

  if (c.funcao != 3 && c.funcao != 4) { erro("funcao Modbus deve ser 3 ou 4"); return; }
  if (c.baud < 1200 || c.baud > 500000) { erro("velocidade fora de faixa"); return; }
  if (c.contagensPorVolta[0] < 1.0f || c.contagensPorVolta[1] < 1.0f) {
    erro("contagens por volta invalidas"); return;
  }

  encoderPendente = c;
  enfileirar(CMD_APLICAR_ENCODER);
}

// Configuracao de encoder guardada por uma versao ANTERIOR continua
// valendo depois de atualizar o firmware: o NVS ganha do padrao novo.
// Quem atualizou de uma versao que apontava para outro registrador fica
// perguntando no lugar errado para sempre, sem nada na tela dizendo
// isso. Este e o botao que desfaz.
// Assentamento pelo encoder. Parametro que mexe em movimento so muda com
// o robo parado -- e o core 1 confere de novo na hora de aplicar.
// Aferir a engrenagem eletronica pelo encoder, sem transferidor.
// Limpa o aviso de travamento depois que o operador resolveu.
static void handleTravamentoOk() {
  registrarContatoOperador();
  correcaoLimparTravamento();
  ok();
}

// =====================================================================
//  ZERO ABSOLUTO -- a pagina escondida
//
//  Fica atras de um cadeado na tela porque errar aqui desloca a area util
//  inteira: o zero e a origem de onde os limites de curso sao contados.
//  Nao e segredo nem senha -- e um tranco para nao se mexer sem querer.
// =====================================================================
static void handleZeroConfig() {
  registrarContatoOperador();
  if (!exigirManual()) return;

  ConfigZero c = configZero;
  c.sincronizar     = argL("sin", c.sincronizar ? 1 : 0) != 0;
  c.irParaZero      = argL("ir",  c.irParaZero  ? 1 : 0) != 0;
  c.toleranciaGraus = argF("tol", c.toleranciaGraus);
  if (c.toleranciaGraus < 0.05f || c.toleranciaGraus > 10.0f) {
    erro("a tolerancia do zero deve ficar entre 0,05 e 10 graus"); return;
  }
  configZero.sincronizar     = c.sincronizar;
  configZero.irParaZero      = c.irParaZero;
  configZero.toleranciaGraus = c.toleranciaGraus;
  salvarConfiguracoes();
  definirMensagem("Ao ligar: %s%s",
                  c.sincronizar ? "recupera a posicao pelo encoder" : "nao recupera a posicao",
                  c.sincronizar && c.irParaZero ? " e vai para 0 grau" : "");
  ok();
}

static void handleEnsinarZero() {
  registrarContatoOperador();
  if (!exigirManual()) return;
  const long j = argL("j", 0);
  if (j != 1 && j != 2) { erro("junta invalida"); return; }
  if (!server.hasArg("g")) { erro("informe em quantos graus a junta esta"); return; }
  const float g = argF("g", 0.0f);
  if (g < -720.0f || g > 720.0f) { erro("angulo fora de faixa"); return; }
  enfileirar(CMD_ENSINAR_ZERO, j, 0, g);
}

static void handleEsquecerZero() {
  registrarContatoOperador();
  if (!exigirManual()) return;
  enfileirar(CMD_ESQUECER_ZERO, argL("j", 0));
}

// ---------------------------------------------------------------------
// Modo aprendizado. O handler NAO chama aprenderEntrar(): entrar corta o
// torque dos servos, e torque e do core 1. Aqui so se enfileira.
static void handleAprender() {
  registrarContatoOperador();
  if (!exigirManual()) return;
  const long on = argL("on", -1);
  if (on < -1 || on > 1) { erro("valor invalido"); return; }
  enfileirar(CMD_APRENDER, on);
}

static void handleCorrecao() {
  registrarContatoOperador();
  if (!exigirManual()) return;

  ConfigCorrecao c = configCorrecao;
  c.ativa            = argL("on",   c.ativa  ? 1 : 0) != 0;
  c.vigiar           = argL("vig",  c.vigiar ? 1 : 0) != 0;
  c.toleranciaGraus  = argF("tol",  c.toleranciaGraus);
  c.maxCorrecaoGraus = argF("max",  c.maxCorrecaoGraus);
  c.alertaGraus      = argF("alr",  c.alertaGraus);
  c.tentativas       = (uint8_t)constrain(argL("tent", c.tentativas), 1, 10);

  // Numeros fora de faixa aqui viram retoque gigante no motor. A porta
  // recusa em vez de deixar o core 1 receber lixo.
  if (c.toleranciaGraus < 0.01f || c.toleranciaGraus > 5.0f) {
    erro("tolerancia deve ficar entre 0,01 e 5 graus"); return;
  }
  if (c.maxCorrecaoGraus < c.toleranciaGraus || c.maxCorrecaoGraus > 15.0f) {
    erro("o teto do retoque deve ficar entre a tolerancia e 15 graus"); return;
  }
  if (c.alertaGraus < 0.05f || c.alertaGraus > 30.0f) {
    erro("o aviso deve ficar entre 0,05 e 30 graus"); return;
  }

  configCorrecao = c;
  salvarConfiguracoes();
  definirMensagem("Assentamento %s: tolerancia %.2f grau, teto %.2f grau",
                  c.ativa ? "ligado" : "desligado",
                  (double)c.toleranciaGraus, (double)c.maxCorrecaoGraus);
  ok();
}

// ---------------------------------------------------------------------
// O habilita (SON), agora que ele mora no barramento.
//
// O registrador nao vem de fabrica adivinhado: vem PROVADO na bancada
// com ferramentas/teste_rs485 (modos d / d2 / s). Esta porta existe para
// gravar o numero que saiu de la, nao para experimentar em producao --
// escrever em parametro errado de um servo drive troca engrenagem
// eletronica, modo de controle ou limite de torque, e isso nao se desfaz
// pela tela.
// ---------------------------------------------------------------------
static void handleSonConfig() {
  registrarContatoOperador();
  if (!exigirManual()) return;

  ConfigSon c = configSon;
  c.reg        = (uint16_t)constrain(argL("reg", c.reg), 0, 65535);
  c.valLiga    = (uint16_t)constrain(argL("liga", c.valLiga), 0, 65535);
  c.valDesliga = (uint16_t)constrain(argL("desl", c.valDesliga), 0, 65535);
  c.funcao16   = argL("f16", c.funcao16 ? 1 : 0) != 0;

  if (c.valLiga == c.valDesliga) {
    erro("habilita e desabilita nao podem ter o mesmo valor"); return;
  }
  // Trocar o registrador com o braco energizado escreveria o desabilita
  // no endereco novo e deixaria o antigo ligado, sem ninguem sabendo.
  if (servosLigados) {
    erro("desabilite os servos antes de mexer no registrador do habilita");
    return;
  }

  configSon = c;
  salvarConfiguracoes();
  if (c.reg == 0)
    definirMensagem("Habilita sem registrador: a maquina nao energiza ate configurar");
  else
    definirMensagem("Habilita no registrador %u (liga=%u, desliga=%u, funcao %s)",
                    (unsigned)c.reg, (unsigned)c.valLiga, (unsigned)c.valDesliga,
                    c.funcao16 ? "16" : "06");
  ok();
}

static void handleEncoderPadroes() {
  registrarContatoOperador();
  if (!exigirManual()) return;

  ConfigEncoder c   = configEncoder;
  c.baud            = ENC_BAUD_PADRAO;
  c.paridade        = ENC_PARIDADE_PADRAO;
  c.funcao          = ENC_FUNCAO_PADRAO;
  c.periodoMs       = ENC_PERIODO_PADRAO;
  c.trintaEDois     = true;
  c.baixaPrimeiro   = ENC_BAIXA_PRIMEIRO;
  c.id[0]  = 1;                c.id[1]  = 2;
  c.reg[0] = ENC_REG_PADRAO;   c.reg[1] = 0;   // junta 2 = nao ligada
  c.contagensPorVolta[0] = ENC_CONTAGENS_PADRAO;
  c.contagensPorVolta[1] = ENC_CONTAGENS_PADRAO;

  encoderPendente = c;
  enfileirar(CMD_APLICAR_ENCODER);
}

// Autoteste da linha, DENTRO do sistema rodando. So pede; quem executa e
// a tarefa do encoder, no core 0 -- ela mexe no modo da UART e nos pinos
// do transceptor, e fazer isso daqui por baixo de uma leitura em
// andamento corromperia o quadro.
static void handleEncoderTestar() {
  registrarContatoOperador();
  if (!exigirManual()) return;
  encoderPedirTeste();
  server.send(200, "text/plain", "testando a linha");
}

static void handleEncoderTeste() {
  registrarContatoOperador();
  char rel[560];
  encoderRelatorio(rel, sizeof(rel));
  server.send(200, "text/plain",
              encoderTesteRodando() ? "testando a linha..." : rel);
}

// Cacada do registrador: marcar, o operador move o braco, comparar.
static void handleEncoderCacar() {
  registrarContatoOperador();
  if (!exigirManual()) return;
  encoderPedirCacada(argL("comparar", 0) != 0);
  server.send(200, "text/plain", "procurando");
}

static void handleEncoderZerar() {
  registrarContatoOperador();
  enfileirar(CMD_ENCODER_ZERAR, argL("j", 0));
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
  char json[400];
  const uint8_t e = (uint8_t)armEstado();
  // A mensagem do cartao e a que MAIS traz aspas: `programa "peca 1"
  // salvo` sai toda vez que alguem grava um arquivo.
  char msgSegura[192];
  jsonTexto(msgSegura, sizeof(msgSegura), armMensagem());
  snprintf(json, sizeof(json),
    "{\"estado\":\"%s\",\"ocupado\":%s,\"seq\":%lu,"
    "\"totalMB\":%lu,\"livreMB\":%lu,\"msg\":\"%s\"}",
    (e < 5) ? NOMES_ARM[e] : "?",
    armOcupado() ? "true" : "false",
    (unsigned long)armSequencia(),
    (unsigned long)(armBytesTotais() / (1024ULL * 1024ULL)),
    (unsigned long)(armBytesLivres() / (1024ULL * 1024ULL)),
    msgSegura);
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

// ---------------------------------------------------------------------
// PREVIA de um programa do cartao: le o arquivo e DESENHA, sem trocar o
// programa que esta na maquina. Ver a peca errada e barato; carregar a
// peca errada custa uma chapa.
// ---------------------------------------------------------------------
static void handleSdPrever() {
  registrarContatoOperador();
  const String nome = server.arg("nome");
  if (!armNomeValido(nome.c_str())) { erro("nome invalido"); return; }
  if (!armSolicitar(TAR_PREVER_PROG, nome.c_str())) {
    erro("cartao ocupado ou ausente");
    return;
  }
  ok();
}

// A previa fica na area de troca ate alguem pedir outra. Devolve os
// pontos ja em milimetros, com os elos com que o arquivo foi feito --
// e a comparacao desses elos com os da maquina que avisa "esta peca nao
// e desta maquina" antes de o arco abrir.
static void handleSdPrevia() {
  registrarContatoOperador();
  const uint8_t n = armStagingN();
  const float e1 = armStagingElo1(), e2 = armStagingElo2();

  String out;
  out.reserve(120 + (size_t)n * 34);
  char cab[160];
  snprintf(cab, sizeof(cab),
           "{\"n\":%u,\"l1\":%.1f,\"l2\":%.1f,\"l1Maq\":%.1f,\"l2Maq\":%.1f,\"pts\":[",
           (unsigned)n, e1, e2, elo1Mm, elo2Mm);
  out += cab;

  // Desenhados com os elos DO ARQUIVO, nao com os da maquina: a miniatura
  // tem de mostrar a peca como ela foi feita. Se os elos diferem, o aviso
  // e justamente essa diferenca -- e desenhar com os elos errados a
  // esconderia.
  const float a1 = (e1 > 0.0f) ? e1 : elo1Mm;
  const float a2 = (e2 > 0.0f) ? e2 : elo2Mm;
  const Ponto* pts = armStagingPontos();
  for (uint8_t i = 0; i < n; i++) {
    const float t1 = passosParaGraus(J1, pts[i].p1);
    const float t2 = passosParaGraus(J2, pts[i].p2);
    const float r1 = t1 * (float)M_PI / 180.0f;
    const float r12 = (t1 + t2) * (float)M_PI / 180.0f;
    const float x = a1 * cosf(r1) + a2 * cosf(r12);
    const float y = a1 * sinf(r1) + a2 * sinf(r12);
    char item[40];
    snprintf(item, sizeof(item), "%s{\"x\":%.0f,\"y\":%.0f,\"s\":%u}",
             i ? "," : "", x, y, (unsigned)pts[i].soldaAteProximo);
    out += item;
  }
  out += "]}";
  server.send(200, "application/json", out);
}

// Restaurar a configuracao da copia automatica do cartao.
//
// O caminho de volta do espelho. Sem ele a copia seria so-escrita, e
// copia que ninguem consegue ler nao e copia -- e um arquivo.
//
// Nome fixo e nao vem do pedido: o operador restaura O espelho, nao um
// nome que ele digitou. Um nome livre aqui abriria a porta para carregar
// um arquivo de configuracao de outra maquina por engano.
static void handleCfgRestaurar() {
  registrarContatoOperador();
  if (!exigirManual()) return;
  if (armEstado() != ARM_PRONTO) { erro("nenhum cartao pronto"); return; }
  if (armOcupado())              { erro("o cartao esta ocupado, aguarde"); return; }
  if (!armSolicitar(TAR_CARREGAR_CONFIG, CFG_CARTAO_NOME)) {
    erro("nao consegui pedir a leitura ao cartao"); return;
  }
  logEvento("configuracao restaurada da copia do cartao");
  ok();
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
  "{\"name\":\"Robo2dof\",\"short_name\":\"Robo2dof\","
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
  server.on("/api/prog/pausar",   HTTP_POST, handleProgPausar);
  server.on("/api/prog/desfazer", HTTP_POST, handleProgDesfazer);
  server.on("/api/prog/repetir",  HTTP_POST, handleProgRepetir);
  server.on("/api/manutencao/ok", HTTP_POST, handleManutencaoOk);
  server.on("/api/mesa/canto",    HTTP_POST, handleMesaCanto);
  server.on("/api/mesa/limpar",   HTTP_POST, handleMesaLimpar);
  server.on("/api/calibracao",    HTTP_GET,  handleCalibracao);
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
  server.on("/api/apagar/tudo",   HTTP_POST, handleApagarTudo);

  server.on("/api/sd",            HTTP_GET,  handleSdEstado);
  server.on("/api/sd/lista",      HTTP_GET,  handleSdLista);
  server.on("/api/sd/salvar",     HTTP_POST, handleSdSalvar);
  server.on("/api/sd/carregar",   HTTP_POST, handleSdCarregar);
  server.on("/api/sd/prever",     HTTP_POST, handleSdPrever);
  server.on("/api/sd/previa",     HTTP_GET,  handleSdPrevia);
  server.on("/api/sd/apagar",     HTTP_POST, handleSdApagar);
  server.on("/api/sd/montar",     HTTP_POST, handleSdMontar);
  server.on("/api/cfg/restaurar", HTTP_POST, handleCfgRestaurar);

  server.on("/manifest.webmanifest", HTTP_GET, handleManifesto);
  server.on("/icone.svg",            HTTP_GET, handleIcone);

  server.on("/api/calib/iniciar",   HTTP_POST, handleCalibIni);
  server.on("/api/calib/confirmar", HTTP_POST, handleCalibConf);
  server.on("/api/calib/cancelar",  HTTP_POST, handleCalibCanc);
  server.on("/api/calib/apagar",    HTTP_POST, handleCalibApagar);
  server.on("/api/referenciar",     HTTP_POST, handleReferenciar);
  server.on("/api/sentido",         HTTP_POST, handleSentido);

  server.on("/api/rede",           HTTP_GET,  handleRede);

  server.on("/api/saude",          HTTP_GET,  handleSaude);
  server.on("/api/registro",       HTTP_GET,  handleRegistro);
  server.on("/api/ota",            HTTP_POST, handleOtaFim, handleOtaEnvio);
  server.on("/api/encoder",        HTTP_GET,  handleEncoder);
  server.on("/api/encoder/config", HTTP_POST, handleEncoderConfig);
  server.on("/api/encoder/padroes", HTTP_POST, handleEncoderPadroes);
  server.on("/api/son/config",     HTTP_POST, handleSonConfig);
  server.on("/api/correcao",       HTTP_POST, handleCorrecao);
  server.on("/api/aprender",       HTTP_POST, handleAprender);
  server.on("/api/travamento/ok",  HTTP_POST, handleTravamentoOk);
  server.on("/api/zero/config",    HTTP_POST, handleZeroConfig);
  server.on("/api/zero/ensinar",   HTTP_POST, handleEnsinarZero);
  server.on("/api/zero/esquecer",  HTTP_POST, handleEsquecerZero);
  server.on("/api/encoder/testar", HTTP_POST, handleEncoderTestar);
  server.on("/api/encoder/teste",  HTTP_GET,  handleEncoderTeste);
  server.on("/api/encoder/cacar",  HTTP_POST, handleEncoderCacar);
  server.on("/api/encoder/zerar",  HTTP_POST, handleEncoderZerar);

  server.onNotFound(handleNaoEncontrado);
  server.begin();
  Serial.println("[WEB] Servidor HTTP ouvindo na porta 80.");
}

void servidorAtender() {
  server.handleClient();
}
