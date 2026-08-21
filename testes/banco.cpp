// =====================================================================
//  Banco de testes do firmware RoboCNC v6.
//
//  Compila os modulos REAIS (cinematica, motores, solda, trajetoria,
//  programa, calibracao, estado e o proprio .ino) contra mocks de
//  Arduino / FastAccelStepper / NVS / FreeRTOS, e executa cenarios de
//  operacao para observar o comportamento do sistema.
//
//  Cada cenario imprime PASSA (o firmware fez o esperado) ou ANOMALIA
//  (o firmware fez outra coisa). Uma ANOMALIA aqui e um comportamento
//  reproduzivel, nao uma opiniao.
// =====================================================================
#include "Arduino.h"
#include "estado.h"
#include "motores.h"
#include "cinematica.h"
#include "solda.h"
#include "trajetoria.h"
#include "programa.h"
#include "calibracao.h"
#include "Preferences.h"

extern void setup();
extern void loop();
extern int  g_comandosDescartados;
extern uint32_t g_msgCount;
extern uint32_t g_serialBytes;
extern NvsMock g_nvs;

// ---------------------------------------------------------------------
static int nPassa = 0, nAnomalia = 0;
static const char* secaoAtual = "";

static void secao(const char* t) {
  secaoAtual = t;
  printf("\n\033[1m== %s\033[0m\n", t);
}
static void checar(bool esperado_ocorreu, const char* id, const char* descricao) {
  if (esperado_ocorreu) { nPassa++;    printf("  \033[32mPASSA\033[0m    %-7s %s\n", id, descricao); }
  else                  { nAnomalia++; printf("  \033[31mANOMALIA\033[0m %-7s %s\n", id, descricao); }
}
static void nota(const char* fmt, ...) {
  va_list a; va_start(a, fmt);
  printf("           \033[2m");
  vprintf(fmt, a);
  printf("\033[0m\n");
  va_end(a);
}

// ---------------------------------------------------------------------
// Motor do simulador: avanca o tempo, a fisica dos steppers e o loop().
// ---------------------------------------------------------------------
static const float DT_MS = 1.0f;

static void rodar(uint32_t ms) {
  for (uint32_t i = 0; i < ms; i++) {
    g_millis += (uint32_t)DT_MS;
    if (J1.motor) J1.motor->avancar(DT_MS);
    if (J2.motor) J2.motor->avancar(DT_MS);
    loop();
  }
}
// Simula o navegador vivo: heartbeat HTTP a cada 200 ms.
static void rodarComWeb(uint32_t ms) {
  for (uint32_t i = 0; i < ms; i++) {
    if (i % 200 == 0) registrarContatoWeb();
    rodar(1);
  }
}

// ---------------------------------------------------------------------
// Coloca o robo num estado conhecido: servos ligados, juntas calibradas
// com +/-90 graus de curso, protecoes de fabrica.
// ---------------------------------------------------------------------
static void prepararRoboCalibrado(float grausCurso = 90.0f) {
  registrarContatoWeb();
  enviarComando(CMD_SERVOS, 1);
  rodarComWeb(20);

  J1.calibrada = J2.calibrada = true;
  const long p = (long)(grausCurso * J1.passosPorGrau);
  J1.passosMin = -p; J1.passosMax = p;
  J2.passosMin = -p; J2.passosMax = p;
  recalcularResolucao();
  if (J1.motor) J1.motor->setCurrentPosition(0);
  if (J2.motor) J2.motor->setCurrentPosition(0);
  rodarComWeb(10);
}

static void reiniciarSistema() {
  g_nvs = NvsMock();
  g_millis = 1000;
  g_comandosDescartados = 0;
  setup();
  // Descarta o transiente do boot.
  registrarContatoWeb();
  rodarComWeb(50);
}

// =====================================================================
//  A01 - Parada de emergencia disputando fila com o jog
// =====================================================================
static void teste_A01_fila_de_comandos() {
  secao("A01  Fila de comandos: a parada tem prioridade sobre o jog?");
  reiniciarSistema();
  prepararRoboCalibrado();

  // O core 1 esta ocupado (situacao real: um ciclo longo). Enquanto isso
  // a interface continua mandando heartbeat de jog a cada 100 ms e o
  // operador aperta PARAR.
  int enfileirados = 0;
  for (int i = 0; i < 24; i++) if (enviarComando(CMD_JOG, 1, 1)) enfileirados++;
  const bool paradaAceita = enviarComando(CMD_PARAR);

  checar(paradaAceita, "A01",
         "CMD_PARAR entra na fila mesmo com a fila cheia de jog");
  nota("fila de %d posicoes; %d jogs enfileirados; PARAR %s",
       24, enfileirados, paradaAceita ? "aceito" : "DESCARTADO");
  nota("handleParar() em servidor_web.cpp responde 200 \"ok\" sem olhar o");
  nota("retorno de enviarComando(): a interface diz que parou e nao parou.");
}

// =====================================================================
//  A02 - Jog com os servos desabilitados
// =====================================================================
static void teste_A02_jog_sem_servos() {
  secao("A02  Jog com os drivers desabilitados (SON em nivel baixo)");
  reiniciarSistema();
  prepararRoboCalibrado();

  enviarComando(CMD_SERVOS, 0);           // tira o torque
  rodarComWeb(20);
  const long antes = posicaoJ1();

  for (int i = 0; i < 10; i++) { enviarComando(CMD_JOG, 1, 1); rodarComWeb(100); }
  enviarComando(CMD_JOG, 1, 0);
  rodarComWeb(200);

  const long depois = posicaoJ1();
  checar(depois == antes, "A02",
         "com servos desligados o contador de passos NAO deve andar");
  nota("servosLigados=%d, posicao J1: %ld -> %ld (%+ld passos = %.1f graus)",
       (int)servosLigados, antes, depois, depois - antes,
       (depois - antes) / J1.passosPorGrau);
  nota("O firmware gerou pulsos para um driver desabilitado. O eixo nao");
  nota("se moveu, mas a referencia de calibracao andou: todo limite de");
  nota("curso passa a apontar para o lugar errado.");
}

// =====================================================================
//  A03 - Faixa morta da margem de seguranca
// =====================================================================
static void teste_A03_faixa_morta_da_margem() {
  secao("A03  Faixa da margem de seguranca: da para sair de la?");
  reiniciarSistema();
  prepararRoboCalibrado();

  // Coloca a junta 1 DENTRO da margem: entre grausMin e grausMin+MARGEM.
  const float dentroDaMargem = J1.grausMin + 0.25f;   // MARGEM = 0.5
  J1.motor->setCurrentPosition(grausParaPassos(J1, dentroDaMargem));
  rodarComWeb(10);

  const char* motivo = nullptr;
  const bool valida  = posturaValida(dentroDaMargem, 0.0f, &motivo);
  const float grav   = gravidadeViolacao(dentroDaMargem, 0.0f);

  const long antes = posicaoJ1();
  for (int i = 0; i < 6; i++) { enviarComando(CMD_JOG, 1, +1); rodarComWeb(100); }  // volta pro centro
  enviarComando(CMD_JOG, 1, 0); rodarComWeb(200);
  const long depois = posicaoJ1();

  checar(depois > antes, "A03",
         "o jog de recuperacao deve liberar o movimento que VOLTA pro curso");
  nota("t1 = %.2f graus (limite %.2f, margem %.1f)", dentroDaMargem,
       J1.grausMin, MARGEM_LIMITE_GRAUS);
  nota("posturaValida() = %s (%s)  |  gravidadeViolacao() = %.3f",
       valida ? "true" : "false", motivo ? motivo : "-", grav);
  nota("posturaValida usa grausMin+MARGEM; gravidadeViolacao usa grausMin");
  nota("cru. Na faixa entre os dois a postura e invalida E a gravidade e");
  nota("zero, entao o criterio de recuperacao (gAtual>0.001) nunca vale.");
  nota("Movimento: %ld -> %ld passos.", antes, depois);
}

// =====================================================================
//  A04 - Calibracao com curso ridiculo trava a maquina
// =====================================================================
static void teste_A04_curso_minimo_aceito() {
  secao("A04  Calibracao aceita curso de 0,4 grau e tranca o braco");
  reiniciarSistema();

  enviarComando(CMD_SERVOS, 1); rodarComWeb(20);
  enviarComando(CMD_CALIB_INICIAR); rodarComWeb(10);
  enviarComando(CMD_CALIB_CONFIRMAR); rodarComWeb(10);   // HOME -> zera

  // O operador confirma os quatro limites praticamente sem sair do lugar
  // (11 passos de curso: ajustarCurso() exige apenas > 10).
  J1.motor->setCurrentPosition(-6);
  enviarComando(CMD_CALIB_CONFIRMAR); rodarComWeb(400);  // J1_NEG
  J1.motor->setCurrentPosition(+6);
  enviarComando(CMD_CALIB_CONFIRMAR); rodarComWeb(400);  // J1_POS
  J2.motor->setCurrentPosition(-6);
  enviarComando(CMD_CALIB_CONFIRMAR); rodarComWeb(400);  // J2_NEG
  J2.motor->setCurrentPosition(+6);
  enviarComando(CMD_CALIB_CONFIRMAR); rodarComWeb(400);  // J2_POS
  enviarComando(CMD_CALIB_CONFIRMAR); rodarComWeb(50);   // CONCLUIDO

  const bool aceitou = J1.calibrada && J2.calibrada;
  const float curso1 = J1.grausMax - J1.grausMin;

  checar(!aceitou, "A04",
         "calibracao com curso menor que a margem de seguranca deve ser recusada");
  nota("calibrada=%s, curso medido J1 = %.3f grau (margem exigida = %.1f x2)",
       aceitou ? "SIM" : "nao", curso1, MARGEM_LIMITE_GRAUS);

  if (aceitou) {
    // O braco esta em zero, dentro do curso. Ainda assim...
    const char* m = nullptr;
    const bool ok = posturaValida(0.0f, 0.0f, &m);
    const long antes = posicaoJ1();
    for (int i = 0; i < 6; i++) { enviarComando(CMD_JOG, 1, +1); rodarComWeb(100); }
    enviarComando(CMD_JOG, 1, 0); rodarComWeb(200);
    checar(posicaoJ1() != antes, "A04b",
           "com a calibracao aceita, o jog ainda precisa funcionar");
    nota("posturaValida(0,0) = %s (%s)", ok ? "true" : "false", m ? m : "-");
    nota("grausMin+0.5 = %.2f  >  grausMax-0.5 = %.2f  ->  nenhuma postura",
         J1.grausMin + MARGEM_LIMITE_GRAUS, J1.grausMax - MARGEM_LIMITE_GRAUS);
    nota("passa na validacao. O braco fica trancado nos dois eixos.");
  }
}

// =====================================================================
//  A05 - Perda de conexao no meio de um cordao
// =====================================================================
static void teste_A05_perda_de_conexao_soldando() {
  secao("A05  Queda de Wi-Fi com o programa de solda em execucao");
  reiniciarSistema();
  prepararRoboCalibrado();

  // Dois pontos, cordao ligado entre eles.
  J1.motor->setCurrentPosition(grausParaPassos(J1, 20.0f));
  J2.motor->setCurrentPosition(grausParaPassos(J2, -40.0f));
  rodarComWeb(5);
  enviarComando(CMD_PONTO_GRAVAR); rodarComWeb(10);
  J1.motor->setCurrentPosition(grausParaPassos(J1, 35.0f));
  rodarComWeb(5);
  enviarComando(CMD_PONTO_GRAVAR); rodarComWeb(10);
  enviarComando(CMD_PONTO_SOLDA, 0, 1); rodarComWeb(10);

  enviarComando(CMD_PROG_EXECUTAR, 0);   // com arco
  rodarComWeb(1500);

  const bool soldando = (modoAtual == MODO_EXECUTANDO);
  nota("antes da queda: modo=%d, progRodando=%d, arco=%d, acel J1=%lu",
       (int)modoAtual, (int)progRodando(), (int)soldaLigada(),
       (unsigned long)J1.motor->getAcceleration());
  const uint32_t acelDurante = J1.motor->getAcceleration();

  // O navegador some. Nenhum registrarContatoWeb() a partir daqui.
  rodar(4000);

  checar(!soldaLigada(), "A05a", "o arco deve fechar quando a conexao cai");
  checar(!progRodando(), "A05b",
         "a maquina de estados do programa deve ser encerrada, nao congelada");
  nota("depois da queda: modo=%d, progRodando=%d, progIdx=%u",
       (int)modoAtual, (int)progRodando(), (unsigned)progIndiceAtual());
  nota("supervisionar() chama trajPararReproducao() e trajPararGravacao(),");
  nota("mas nunca progParar(). O programa fica em fase != PARADO com o");
  nota("modo de volta em MANUAL.");

  const uint32_t acelDepois = J1.motor->getAcceleration();
  checar(acelDepois == J1.aceleracao, "A05c",
         "a aceleracao deve voltar ao valor configurado apos a parada");
  nota("aceleracao configurada = %lu; durante o cordao = %lu; depois da",
       (unsigned long)J1.aceleracao, (unsigned long)acelDurante);
  nota("queda = %lu. Quem restaura e progParar(), que nao foi chamado.",
       (unsigned long)acelDepois);
  nota("O jog manual seguinte roda com aceleracao 4x mais alta.");
  (void)soldando;
}

// =====================================================================
//  A06 - "ir para o ponto" nao revalida a postura
// =====================================================================
static void teste_A06_ir_ponto_sem_revalidar() {
  secao("A06  Botao 'ir' de um ponto gravado revalida a postura?");
  reiniciarSistema();
  prepararRoboCalibrado();

  // Grava um ponto valido com a protecao de envelope DESLIGADA (padrao).
  protEnvelope = false;
  J1.motor->setCurrentPosition(grausParaPassos(J1, -70.0f));
  J2.motor->setCurrentPosition(grausParaPassos(J2, -20.0f));
  rodarComWeb(5);
  const char* m = nullptr;
  const bool gravou = progAdicionarPonto(posicaoJ1(), posicaoJ2(), &m);

  float xc, yc, xp, yp;
  cinematicaDireta(-70.0f, -20.0f, xc, yc, xp, yp);
  nota("ponto gravado: t1=-70 t2=-20  ->  ponta em X=%.0f Y=%.0f mm (%s)",
       xp, yp, gravou ? "aceito" : m);

  // Agora o operador liga a protecao de mesa, como o LEIA-ME manda.
  protEnvelope = true;
  const char* mv = nullptr;
  const bool aindaValido = posturaValida(-70.0f, -20.0f, &mv);
  nota("com protecao de mesa ligada esse ponto e %s (%s)",
       aindaValido ? "valido" : "INVALIDO", mv ? mv : "-");

  // Volta o braco para o centro e manda ir ao ponto.
  J1.motor->setCurrentPosition(0);
  J2.motor->setCurrentPosition(0);
  rodarComWeb(5);
  enviarComando(CMD_IR_PARA_PONTO, 0);
  rodarComWeb(30);

  const bool moveu = (modoAtual == MODO_POSICIONANDO) || motoresEmMovimento();
  checar(aindaValido || !moveu, "A06",
         "'ir' para um ponto agora invalido deve ser recusado");
  nota("modo apos o comando = %d (5=CALIBRANDO, 4=POSICIONANDO)", (int)modoAtual);
  nota("CMD_IR_PARA_PONTO em RoboCNC.ino chama moverCoordenado() direto,");
  nota("sem passar por posturaValida(). O LEIA-ME promete validacao em");
  nota("TODO comando de movimento.");
}

// =====================================================================
//  A07 - Deslocamento sem solda atravessa a zona proibida
// =====================================================================
static void teste_A07_deslocamento_atravessa_mesa() {
  secao("A07  Trecho sem solda: o caminho curvo respeita a mesa?");
  reiniciarSistema();
  prepararRoboCalibrado(170.0f);
  protEnvelope = true;
  protDobra    = true;
  envYMin      = -50.0f;
  envRaioMin   = 40.0f;

  // Busca exaustiva: existe algum par de posturas AMBAS validas cuja
  // interpolacao nas juntas -- exatamente o que moverCoordenado() faz --
  // passe por uma postura invalida?
  const float PASSO = 10.0f;
  float A1=0, A2=0, B1=0, B2=0, piorA=0;
  const char* motivoMeio = nullptr;
  bool achou = false;
  long paresValidos = 0;

  for (float a1 = -170; a1 <= 170 && !achou; a1 += PASSO)
  for (float a2 = -170; a2 <= 170 && !achou; a2 += PASSO) {
    if (!posturaValida(a1, a2, nullptr)) continue;
    for (float b1 = -170; b1 <= 170 && !achou; b1 += PASSO)
    for (float b2 = -170; b2 <= 170 && !achou; b2 += PASSO) {
      if (!posturaValida(b1, b2, nullptr)) continue;
      paresValidos++;
      for (int k = 1; k < 40; k++) {
        const float a = (float)k / 40.0f;
        const char* m = nullptr;
        if (!posturaValida(a1 + (b1-a1)*a, a2 + (b2-a2)*a, &m)) {
          A1=a1; A2=a2; B1=b1; B2=b2; piorA=a; motivoMeio=m; achou = true; break;
        }
      }
    }
  }

  checar(!achou, "A07",
         "o caminho entre dois pontos validos nao pode atravessar zona proibida");
  nota("%ld pares de posturas validas examinados (grade de %.0f graus)",
       paresValidos, PASSO);
  if (achou) {
    float xc, yc, xp, yp;
    cinematicaDireta(A1, A2, xc, yc, xp, yp);
    nota("ponto A: t=(%.0f, %.0f)  ponta (%.0f, %.0f) mm  -> VALIDO", A1, A2, xp, yp);
    cinematicaDireta(B1, B2, xc, yc, xp, yp);
    nota("ponto B: t=(%.0f, %.0f)  ponta (%.0f, %.0f) mm  -> VALIDO", B1, B2, xp, yp);
    const float m1 = A1 + (B1-A1)*piorA, m2 = A2 + (B2-A2)*piorA;
    cinematicaDireta(m1, m2, xc, yc, xp, yp);
    nota("a %.0f%% do percurso: t=(%.0f, %.0f)  ponta (%.0f, %.0f) mm  -> %s",
         piorA*100, m1, m2, xp, yp, motivoMeio ? motivoMeio : "invalido");
    nota("progIniciar() valida as PONTAS de todo trecho e a reta inteira dos");
    nota("trechos com solda, mas nunca o interior de um deslocamento; e");
    nota("nada supervisiona a postura enquanto moverCoordenado() executa.");
  }
}

// =====================================================================
//  A08 - Emergencia fisica x rearme dos servos
// =====================================================================
static void teste_A08_estop_rearme() {
  secao("A08  Botao de emergencia fisico segurado: da para religar servos?");

  if (!ESTOP_FISICO_INSTALADO) {
    nota("ESTOP_FISICO_INSTALADO = false em config.h: o teste roda sobre a");
    nota("logica de supervisionar() analisada estaticamente.");
    nota("");
    nota("  if (estop && !emergenciaAtiva) { ... servosHabilitar(false); }");
    nota("");
    nota("A acao so acontece na BORDA. Com o botao ainda pressionado,");
    nota("emergenciaAtiva ja e true, entao CMD_SERVOS(1) chama");
    nota("servosHabilitar(true) e nada volta a desligar. O intertravamento");
    nota("do rele checa !estop, mas jogAtualizar() nao checa nada disso:");
    nota("o braco volta a se mover com a emergencia acionada.");
    checar(false, "A08",
           "com o e-stop acionado, habilitar servos deve ser recusado");
    return;
  }
}

// =====================================================================
//  A09 - Mensagem de recuperacao inundando a serial
// =====================================================================
static void teste_A09_serial_no_loop() {
  secao("A09  definirMensagem() dentro do laco de 1 ms");
  reiniciarSistema();
  prepararRoboCalibrado();

  // Coloca a junta FORA do curso (situacao real: mudanca de resolucao,
  // perda de passos, calibracao refeita) e pede o jog de volta.
  J1.motor->setCurrentPosition(grausParaPassos(J1, J1.grausMax + 5.0f));
  rodarComWeb(5);

  const uint32_t bytes0 = g_serialBytes, msgs0 = g_msgCount, t0 = g_millis;
  for (int i = 0; i < 10; i++) { enviarComando(CMD_JOG, 1, -1); rodarComWeb(100); }
  enviarComando(CMD_JOG, 1, 0); rodarComWeb(50);
  const uint32_t bytes = g_serialBytes - bytes0;
  const uint32_t msgs  = g_msgCount - msgs0;
  const uint32_t dt    = g_millis - t0;
  const float taxa = dt ? (float)msgs * 1000.0f / dt : 0.0f;

  // Criterio: uma mensagem de estado repetida mais de 20x por segundo nao
  // informa nada e so consome UART dentro do laco de controle.
  checar(taxa <= 20.0f, "A09",
         "mensagem de estado nao pode ser reemitida em todo ciclo do loop");
  nota("%u mensagens identicas em %u ms  =  %.0f por segundo", msgs, dt, taxa);
  nota("%u bytes na serial = %.1f bytes/ms; a 115200 8N1 cabem 11,5 bytes/ms",
       bytes, dt ? (float)bytes / dt : 0.0f);
  nota("(%.0f%% da UART ocupada por uma unica mensagem repetida)",
       dt ? (float)bytes / dt / 11.52f * 100.0f : 0.0f);
  nota("Quando o buffer de TX enche, Serial.print() bloqueia -- e quem");
  nota("trava e o loop() do core 1, o mesmo que roda supervisionar().");
  nota("Origem: definirMensagem(\"Junta %%u fora da area util: voltando\")");
  nota("em motores.cpp, chamada a cada ciclo enquanto durar a recuperacao.");
}

// =====================================================================
//  A10 - Tamanho do JSON de status
// =====================================================================
static void teste_A10_json_status() {
  secao("A10  Buffer do JSON de /api/status");

  // Reproduz o pior caso do snprintf de handleStatus() com valores
  // realistas de maquina grande e mensagem cheia.
  char json[4096];
  char msg[96];
  memset(msg, 'x', sizeof(msg) - 1); msg[sizeof(msg) - 1] = '\0';

  const int n = snprintf(json, sizeof(json),
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
    "REPRODUZINDO", "J1_VOLTA_NEG", 2u,
    -2000000L, -2000000L, -359.99f, -359.99f, -1999.9f, -1999.9f,
    "false","false","false","false","false","false","false","false",
    -359.9f, 359.9f, -359.9f, 359.9f,
    1500u, 4294967295UL, 100u, 200u,
    40u, 40u, 100u, "false", 999.9f,
    999.9f, "false","false","false",
    180000UL, 180000UL, 180000UL, 999999UL, 999999UL,
    999999UL, 999.999f, 999999UL, 999.999f,
    180000.f, 180000.f, 9999.9f, 9999.99f, 9999.99f,
    9999.9f, 9999.9f, 90.0f, -9999.9f, 9999.9f,
    msg);

  checar(n < 1024, "A10", "o JSON de status precisa caber no buffer de 1024 bytes");
  nota("pior caso medido: %d bytes. Buffer declarado em servidor_web.cpp: 1024.", n);
  if (n >= 1024) {
    nota("snprintf trunca sem erro: a resposta sai como JSON invalido, o");
    nota("r.json() do navegador lanca excecao, o contador 'quedas' sobe e a");
    nota("interface anuncia 'sem comunicacao' com o robo funcionando.");
  }
}

// =====================================================================
//  A11 - Cancelar a calibracao depois de rezerar
// =====================================================================
static void teste_A11_cancelar_calibracao() {
  secao("A11  Cancelar a calibracao no meio deixa limites coerentes?");
  reiniciarSistema();
  prepararRoboCalibrado();
  salvarConfiguracoes();       // grava o curso +/-90 em NVS

  nota("calibracao valida em NVS: J1 de %.1f a %.1f graus, zero na posicao %ld",
       J1.grausMin, J1.grausMax, posicaoJ1());

  // O operador comeca uma nova calibracao e define o HOME 30 graus adiante.
  enviarComando(CMD_CALIB_INICIAR); rodarComWeb(10);
  J1.motor->setCurrentPosition(grausParaPassos(J1, 30.0f));
  rodarComWeb(5);
  enviarComando(CMD_CALIB_CONFIRMAR); rodarComWeb(10);   // zera AQUI

  // ...e desiste.
  enviarComando(CMD_CALIB_CANCELAR); rodarComWeb(20);

  const float t1 = passosParaGraus(J1, posicaoJ1());
  nota("apos cancelar: calibrada=%d, limites %.1f a %.1f, posicao logica %.1f",
       (int)J1.calibrada, J1.grausMin, J1.grausMax, t1);
  nota("O braco esta fisicamente onde estava 30 graus adiante do zero");
  nota("antigo, mas o contador foi rezerado e nunca desfeito.");

  checar(!J1.calibrada, "A11",
         "cancelar depois de rezerar deve invalidar a calibracao restaurada");
  nota("calibCancelar() chama carregarConfiguracoes() e recupera passosMin/");
  nota("passosMax do NVS, que se referem ao zero ANTIGO. zerarPosicoes()");
  nota("nao e desfeito: os limites passam a proteger a regiao errada, com");
  nota("erro igual a distancia entre os dois zeros.");
}

// =====================================================================
//  A12 - Reproducao de trajetoria sem exigir servos
// =====================================================================
static void teste_A12_reproduzir_sem_servos() {
  secao("A12  Reproducao de trajetoria exige servos habilitados?");
  reiniciarSistema();
  prepararRoboCalibrado();

  enviarComando(CMD_GRAVAR_INICIAR); rodarComWeb(10);
  for (int i = 0; i < 8; i++) { enviarComando(CMD_JOG, 1, 1); rodarComWeb(60); }
  enviarComando(CMD_JOG, 1, 0); rodarComWeb(100);
  enviarComando(CMD_GRAVAR_PARAR); rodarComWeb(20);
  nota("trajetoria gravada: %u pontos, %.2f s",
       (unsigned)trajPontos(), trajDuracaoMs() / 1000.0f);

  enviarComando(CMD_SERVOS, 0); rodarComWeb(20);
  enviarComando(CMD_REPRODUZIR); rodarComWeb(30);

  const bool comecou = trajReproduzindo() || modoAtual == MODO_REPRODUZINDO;
  checar(!comecou, "A12",
         "reproduzir com os drivers desabilitados deve ser recusado");
  nota("servosLigados=%d, reproduzindo=%d, modo=%d",
       (int)servosLigados, (int)trajReproduzindo(), (int)modoAtual);
  nota("CMD_PROG_EXECUTAR checa servosLigados quando ha arco; a reproducao");
  nota("de trajetoria (que tambem aciona o rele) nao checa nada.");
  enviarComando(CMD_PARAR); rodarComWeb(20);
}

// =====================================================================
//  A13 - Condicionamento da cinematica inversa perto da singularidade
// =====================================================================
static void teste_A13_troca_de_cotovelo() {
  secao("A13  Cordao perto do braco esticado: a reta se mantem?");
  reiniciarSistema();
  prepararRoboCalibrado();
  protEnvelope = false;
  protDobra    = true;

  // Curso assimetrico da junta 2 -- perfeitamente normal numa maquina
  // real, e o que a calibracao mede.
  J2.passosMin = grausParaPassos(J2, -150.0f);
  J2.passosMax = grausParaPassos(J2,   25.0f);
  J1.passosMin = grausParaPassos(J1, -150.0f);
  J1.passosMax = grausParaPassos(J1,  150.0f);
  recalcularResolucao();
  nota("curso: J1 %.0f..%.0f, J2 %.0f..%.0f graus",
       J1.grausMin, J1.grausMax, J2.grausMin, J2.grausMax);

  // Varre retas candidatas e mede o maior salto de theta2 entre dois
  // pontos consecutivos da interpolacao de 1,5 mm.
  float maiorSalto = 0.0f, sx0=0, sy0=0, sx1=0, sy1=0, ondeMm = 0;
  float t2Antes = 0, t2Depois = 0, raioNoSalto = 0;
  bool  trocouRamo = false, algumTrocouRamo = false;

  for (float y = -300; y <= 300; y += 20)
  for (float x0 = -350; x0 <= 350; x0 += 50) {
    const float x1 = x0 + 150.0f;      // cordao de 150 mm em X
    const float y1 = y;
    float r1 = 0, r2 = 0;
    const char* m = nullptr;
    if (!resolverXY(x0, y, 0, 0, r1, r2, &m)) continue;
    float ant2 = r2; bool completa = true;
    float salto = 0, onde = 0, a2 = 0, d2 = 0, raio = 0; bool ramo = false;
    const int N = (int)(150.0f / PASSO_INTERP_MM);
    for (int k = 1; k <= N; k++) {
      const float a = (float)k / N;
      const float x = x0 + (x1-x0)*a, yy = y + (y1-y)*a;
      float t1, t2;
      if (!resolverXY(x, yy, r1, r2, t1, t2, &m)) { completa = false; break; }
      const float d = fabsf(t2 - ant2);
      if ((ant2 > 0.5f && t2 < -0.5f) || (ant2 < -0.5f && t2 > 0.5f)) {
        algumTrocouRamo = true;
      }
      if (d > salto) {
        salto = d; onde = a * 150.0f; a2 = ant2; d2 = t2;
        raio = sqrtf(x*x + yy*yy);
        ramo = (ant2 > 0.5f && t2 < -0.5f) || (ant2 < -0.5f && t2 > 0.5f);
      }
      ant2 = t2; r1 = t1; r2 = t2;
    }
    if (completa && salto > maiorSalto) {
      maiorSalto = salto; sx0=x0; sy0=y; sx1=x1; sy1=y1; ondeMm = onde;
      t2Antes = a2; t2Depois = d2; trocouRamo = ramo; raioNoSalto = raio;
    }
  }

  // Um passo de 1,5 mm que exija mais de 5 graus de junta ja e maior que
  // o dobro do que o resto do cordao pede: o seguidor nao acompanha.
  checar(maiorSalto < 5.0f, "A13",
         "um passo de 1,5 mm do cordao nao pode exigir salto grande de junta");
  nota("pior cordao encontrado: (%.0f, %.0f) -> (%.0f, %.0f) mm", sx0, sy0, sx1, sy1);
  nota("maior degrau: theta2 de %.1f para %.1f graus (%.1f graus) num passo de",
       t2Antes, t2Depois, maiorSalto);
  nota("%.1f mm, a %.0f mm do inicio, com a ponta a %.0f mm da base",
       PASSO_INTERP_MM, ondeMm, raioNoSalto);
  nota("(alcance maximo do braco = %.0f mm; troca de ramo do cotovelo neste",
       elo1Mm + elo2Mm);
  nota("degrau: %s; em algum ponto da varredura: %s)",
       trocouRamo ? "SIM" : "nao", algumTrocouRamo ? "SIM" : "nao");
  nota("");
  nota("Perto do braco esticado a cinematica inversa e mal condicionada:");
  nota("milimetros viram dezenas de graus. velSeg1/velSeg2 em prepararReta()");
  nota("sao calculados pela MEDIA do trecho (delta total / tempo total), entao");
  nota("nessa regiao o motor nao alcanca o setpoint, a ponta corta caminho e");
  nota("o cordao deixa de ser reto -- exatamente o que a interpolacao");
  nota("cartesiana existia para garantir. retaPercorrivel() nao reprova:");
  nota("todas as posturas do caminho sao validas, o problema e a derivada.");
  nota("Falta recusar cordao que passe perto de |r| = L1+L2 e travar o ramo");
  nota("do cotovelo no inicio do trecho em vez de reescolher a cada passo.");
}

// =====================================================================
//  A14 - Config aplicada pelo core 0 durante movimento
// =====================================================================
static void teste_A14_config_durante_movimento() {
  secao("A14  /api/config altera a resolucao com o braco em movimento");
  reiniciarSistema();
  prepararRoboCalibrado();

  J1.motor->setCurrentPosition(grausParaPassos(J1, 45.0f));
  rodarComWeb(5);
  const float antes = passosParaGraus(J1, posicaoJ1());
  const long  passos = posicaoJ1();

  // Isto e literalmente o que handleConfig() faz, no core 0, sem fila e
  // sem checar o modo:
  J1.passosPorVolta = J1.passosPorVolta * 2;
  recalcularResolucao();

  const float depois = passosParaGraus(J1, posicaoJ1());
  checar(fabsf(antes - depois) < 0.1f, "A14",
         "mudar a resolucao nao pode teleportar a posicao logica do braco");
  nota("mesma posicao fisica (%ld passos): %.1f graus  ->  %.1f graus",
       passos, antes, depois);
  nota("limites recalculados junto: %.1f..%.1f graus", J1.grausMin, J1.grausMax);
  nota("handleConfig() roda no core 0 e escreve J1/J2 e chama");
  nota("recalcularResolucao() direto, sem passar pela fila de comandos e");
  nota("sem exigir modoAtual == MANUAL. Se acontecer durante um cordao, o");
  nota("grausParaPassos() do proximo setpoint muda de escala no meio.");
}

// =====================================================================
int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);
  printf("\n\033[1mBANCO DE TESTES - RoboCNC v6\033[0m\n");
  printf("firmware real + mocks de Arduino/FastAccelStepper/NVS/FreeRTOS\n");

  teste_A01_fila_de_comandos();
  teste_A02_jog_sem_servos();
  teste_A03_faixa_morta_da_margem();
  teste_A04_curso_minimo_aceito();
  teste_A05_perda_de_conexao_soldando();
  teste_A06_ir_ponto_sem_revalidar();
  teste_A07_deslocamento_atravessa_mesa();
  teste_A08_estop_rearme();
  teste_A09_serial_no_loop();
  teste_A10_json_status();
  teste_A11_cancelar_calibracao();
  teste_A12_reproduzir_sem_servos();
  teste_A13_troca_de_cotovelo();
  teste_A14_config_durante_movimento();

  printf("\n\033[1mRESULTADO: %d passaram, %d anomalias\033[0m\n\n", nPassa, nAnomalia);
  (void)secaoAtual;
  return 0;
}
