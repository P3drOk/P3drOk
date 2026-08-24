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
#include "armazenamento.h"
#include "WebServer.h"
#include "rede.h"
#include "WiFi.h"
#include "ESPmDNS.h"
#include "DNSServer.h"
#include "HardwareSerial.h"
#include "driver/uart.h"
#include "encoder.h"
#include "Preferences.h"
#include "FS.h"
#include <string>
#include <stdlib.h>

extern void setup();
extern void loop();
extern int  g_comandosDescartados;
extern uint32_t g_msgCount;
extern uint32_t g_serialBytes;
extern bool g_serialSilencioso;
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
    // A tarefa de cartao roda no core 0; aqui ela e bombeada a mao, um
    // ciclo por milissegundo. O mock de sistema de arquivos e
    // instantaneo, entao o que se testa e a logica, nao a latencia.
    armCicloTeste();
    // Idem para a tarefa de rede: no ESP32 ela vive dentro de
    // tarefaRede(), que o mock de FreeRTOS nao executa.
    redeAtender();
    encoderCicloTeste();
  }
}
// Simula o navegador vivo: heartbeat HTTP a cada 200 ms.
static void rodarComWeb(uint32_t ms) {
  for (uint32_t i = 0; i < ms; i++) {
    if (i % 200 == 0) registrarContatoOperador();
    rodar(1);
  }
}

// ---------------------------------------------------------------------
// Coloca o robo num estado conhecido: servos ligados, juntas calibradas
// com +/-90 graus de curso, protecoes de fabrica.
// ---------------------------------------------------------------------
static void prepararRoboCalibrado(float grausCurso = 90.0f) {
  registrarContatoOperador();
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

// Percorre o assistente inteiro, colocando os limites onde o teste
// mandar. 'cursoReal' e o que o operador mediu com transferidor
// (0 = nao aferir); 'home1/home2' e o angulo declarado na referencia.
static bool rodarAssistente(long passosNeg, long passosPos,
                            float home1, float home2,
                            float cursoReal1, float cursoReal2) {
  // Esperar por tempo fixo nao serve: entre uma etapa e a outra o eixo
  // volta ao zero, e quanto maior o curso mais isso demora. Espera-se a
  // ETAPA mudar.
  auto ateEtapa = [&](EstadoCalib alvo) {
    uint32_t t = 0;
    while (estadoCalib != alvo && t < 20000) { rodarComWeb(20); t += 20; }
    return estadoCalib == alvo;
  };

  enviarComando(CMD_CALIB_INICIAR);
  ateEtapa(CAL_HOME);
  enviarComando(CMD_CALIB_CONFIRMAR, 0, 0, home1, home2);
  ateEtapa(CAL_J1_NEG);

  J1.motor->setCurrentPosition(passosNeg);
  enviarComando(CMD_CALIB_CONFIRMAR); ateEtapa(CAL_J1_POS);
  J1.motor->setCurrentPosition(passosPos);
  enviarComando(CMD_CALIB_CONFIRMAR); ateEtapa(CAL_J2_NEG);
  J2.motor->setCurrentPosition(passosNeg);
  enviarComando(CMD_CALIB_CONFIRMAR); ateEtapa(CAL_J2_POS);
  J2.motor->setCurrentPosition(passosPos);
  enviarComando(CMD_CALIB_CONFIRMAR); ateEtapa(CAL_CONCLUIDO);

  enviarComando(CMD_CALIB_CONFIRMAR, 0, 0, cursoReal1, cursoReal2);
  const bool fim = ateEtapa(CAL_INATIVO);
  rodarComWeb(60);
  return fim;
}

// Religar a maquina SEM apagar a memoria nao volatil: e assim que se
// testa se algo gravado volta sozinho na proxima partida.
static void reiniciarSistemaMantendoNvs();

static void reiniciarSistema() {
  g_nvs = NvsMock();
  g_fs  = FsMock();
  armReiniciarTeste();
  encoderReiniciarTeste();
  // O barramento RS485 tambem e estado: um cenario que terminou com o
  // driver mudo deixava o seguinte gastando o tempo esgotado de cada
  // leitura, e o relogio do banco corria mais rapido que o movimento.
  // Cada cenario comeca com o driver 1 respondendo e o 2 ausente, que e
  // a bancada do operador.
  g_uart.escravo[0] = EscravoModbus{};
  g_uart.escravo[1] = EscravoModbus{};
  g_uart.escravo[1].existe = false;
  g_uart.moduloLigado = false;
  g_uart.pinoRe       = -1;
  g_millis = 1000;
  g_comandosDescartados = 0;
  setup();
  // Cada cenario comeca com os geradores de pulso no estado de boot: o
  // mock reaproveita os objetos entre setup()s, entao o movimento
  // residual de um teste vazaria para o seguinte.
  if (J1.motor) J1.motor->reiniciar();
  if (J2.motor) J2.motor->reiniciar();
  // progLimpar()/trajLimpar() zeram os buffers estaticos dos modulos, que
  // sobrevivem a um setup() e vazariam pontos de um cenario para o outro.
  progLimpar();
  trajLimpar();
  // No ESP32 o boot reinicializa as variaveis globais; aqui elas
  // sobrevivem ao setup(). Um cenario que termina gravando deixava o
  // seguinte fora do modo manual, e todo pedido de ajuste era recusado
  // por um motivo que nao era o do teste.
  modoAtual = MODO_MANUAL;
  solicitarParada();
  limparFilaComandos();
  // Descarta o transiente do boot.
  registrarContatoOperador();
  rodarComWeb(50);
}

static void reiniciarSistemaMantendoNvs() {
  const NvsMock guardado = g_nvs;
  reiniciarSistema();
  g_nvs = guardado;
  carregarConfiguracoes();
  rodarComWeb(50);
}

// =====================================================================
//  A01 - Parada de emergencia disputando fila com o jog
// =====================================================================
static void teste_A01_fila_de_comandos() {
  secao("A01  Fila de comandos: a parada tem prioridade sobre o jog?");
  reiniciarSistema();
  prepararRoboCalibrado();

  // Poe o braco em movimento de verdade: jog continuo na junta 1.
  for (int i = 0; i < 5; i++) { enviarComando(CMD_JOG, 1, 1); rodarComWeb(60); }
  const bool andando = motoresEmMovimento();

  // Agora a fila enche de heartbeat de jog (a interface manda um a cada
  // 100 ms por eixo e o core 1 esta ocupado), e o operador aperta PARAR.
  int enfileirados = 0;
  for (int i = 0; i < 40; i++) if (enviarComando(CMD_JOG, 1, 1)) enfileirados++;
  const bool filaCheia = !enviarComando(CMD_JOG, 1, 1);

  // handleParar() nao enfileira: escreve a flag que o loop() testa antes
  // de drenar a fila.
  solicitarParada();
  rodarComWeb(5);
  const float velLogoApos = J1.motor ? fabsf(J1.motor->getCurrentSpeedInMilliHz() / 1000.0f) : 0;
  rodarComWeb(1200);   // tempo de sobra para a rampa de desaceleracao

  checar(andando && filaCheia && !motoresEmMovimento(), "A01",
         "a parada tem que agir mesmo com a fila de comandos cheia");
  nota("braco em movimento antes: %s | %d jogs enfileirados ate encher",
       andando ? "sim" : "nao", enfileirados);
  nota("apos solicitarParada(): 5 ms depois ainda a %.0f Hz (rampa de", velLogoApos);
  nota("desaceleracao), 1,2 s depois motores %s, arco %s",
       motoresEmMovimento() ? "AINDA ANDANDO" : "parados",
       soldaLigada() ? "LIGADO" : "desligado");
  nota("A flag pedidoParada e testada no topo do loop(), fora da fila.");
  nota("handleParar() responde 200; os demais handlers respondem 503");
  nota("quando enviarComando() falha, em vez de mentir \"ok\".");

  // O caminho antigo continua existindo para uso interno, mas agora e o
  // unico que pode ser descartado -- e nao e o que a interface usa.
  const bool viaFila = enviarComando(CMD_PARAR);
  nota("(CMD_PARAR pela fila, so para comparacao: %s)",
       viaFila ? "aceito" : "descartado - era este o furo");
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
  nota("Gerar pulso para driver desabilitado nao move o eixo, mas move o");
  nota("contador -- e e nele que toda a protecao de curso se apoia.");
  nota("Bloqueado pelo portao movimentoLiberado (estado.h), avaliado por");
  nota("supervisionar() e consultado em jogAtualizar().");
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
  nota("As duas contas usam a MESMA margem. Antes, posturaValida() usava");
  nota("grausMin+MARGEM e gravidadeViolacao() usava grausMin cru: na faixa");
  nota("entre os dois a postura era invalida E a gravidade era zero, entao");
  nota("o criterio de recuperacao (gAtual>0.001) nunca liberava a volta.");
  nota("Movimento de recuperacao: %ld -> %ld passos.", antes, depois);
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
  nota("calibrada=%s, curso medido = %.3f grau; minimo exigido = %.0f graus",
       aceitou ? "SIM" : "nao", curso1, CURSO_MINIMO_GRAUS);
  nota("ajustarCurso() mede em GRAUS. O criterio antigo (> 10 passos)");
  nota("aceitava 0,4 grau na resolucao padrao -- menos que os 2 x %.1f de",
       MARGEM_LIMITE_GRAUS);
  nota("margem, o que produzia intervalo util negativo e travava os eixos.");

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

  // O navegador some. Nenhum registrarContatoOperador() a partir daqui.
  rodar(4000);

  checar(!soldaLigada(), "A05a", "o arco deve fechar quando a conexao cai");
  checar(!progRodando(), "A05b",
         "a maquina de estados do programa deve ser encerrada, nao congelada");
  nota("depois da queda: modo=%d, progRodando=%d, progIdx=%u",
       (int)modoAtual, (int)progRodando(), (unsigned)progIndiceAtual());
  nota("Os tres ramos de supervisionar() (alarme, emergencia, conexao) e a");
  nota("parada do operador passam todos por pararTudo(), que encerra");
  nota("programa, trajetoria, gravacao, calibracao e jog de uma vez.");

  const uint32_t acelDepois = J1.motor->getAcceleration();
  const uint32_t acelEsperada = grausPorSegParaHz(J1, J1.aceleracao);
  checar(acelDepois == acelEsperada, "A05c",
         "a aceleracao deve voltar ao valor configurado apos a parada");
  nota("configurada %.0f graus/s2 = %lu passos/s2; durante o cordao = %lu (4x);",
       J1.aceleracao, (unsigned long)acelEsperada, (unsigned long)acelDurante);
  nota("depois da queda = %lu. Sem passar por progParar(), o jog manual",
       (unsigned long)acelDepois);
  nota("seguinte rodaria com a aceleracao 4x ainda ativa.");
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
  nota("CMD_IR_PARA_PONTO passa por irParaPassos(), que confere calibracao,");
  nota("servos, a postura de destino e o interior do caminho ate ela -- e");
  nota("nao a validacao feita quando o ponto foi gravado.");
}

// =====================================================================
//  A07 - Deslocamento sem solda atravessa a zona proibida
// =====================================================================
static void teste_A07_deslocamento_atravessa_mesa() {
  secao("A07  Deslocamento que atravessa zona proibida e recusado?");
  reiniciarSistema();
  prepararRoboCalibrado(170.0f);
  protEnvelope = true;
  protDobra    = true;
  envYMin      = -50.0f;
  envRaioMin   = 40.0f;

  // Busca um par de posturas AMBAS validas cuja interpolacao nas juntas --
  // o caminho que moverCoordenado() realmente percorre -- passe por uma
  // postura invalida.
  const float PASSO = 10.0f;
  float A1=0, A2=0, B1=0, B2=0, piorA=0;
  const char* motivoMeio = nullptr;
  bool achou = false;

  for (float a1 = -170; a1 <= 170 && !achou; a1 += PASSO)
  for (float a2 = -170; a2 <= 170 && !achou; a2 += PASSO) {
    if (!posturaValida(a1, a2, nullptr)) continue;
    for (float b1 = -170; b1 <= 170 && !achou; b1 += PASSO)
    for (float b2 = -170; b2 <= 170 && !achou; b2 += PASSO) {
      if (!posturaValida(b1, b2, nullptr)) continue;
      for (int k = 1; k < 40; k++) {
        const float a = (float)k / 40.0f;
        const char* m = nullptr;
        if (!posturaValida(a1 + (b1-a1)*a, a2 + (b2-a2)*a, &m)) {
          A1=a1; A2=a2; B1=b1; B2=b2; piorA=a; motivoMeio=m; achou = true; break;
        }
      }
    }
  }

  if (!achou) { nota("nenhum par desses na grade -- geometria sem armadilha"); return; }

  float xc, yc, xp, yp;
  cinematicaDireta(A1, A2, xc, yc, xp, yp);
  nota("ponto A: t=(%.0f, %.0f)  ponta (%.0f, %.0f) mm  -> VALIDO", A1, A2, xp, yp);
  cinematicaDireta(B1, B2, xc, yc, xp, yp);
  nota("ponto B: t=(%.0f, %.0f)  ponta (%.0f, %.0f) mm  -> VALIDO", B1, B2, xp, yp);
  const float m1 = A1 + (B1-A1)*piorA, m2 = A2 + (B2-A2)*piorA;
  cinematicaDireta(m1, m2, xc, yc, xp, yp);
  nota("a %.0f%% do percurso: t=(%.0f, %.0f)  ponta (%.0f, %.0f) mm  ->  %s",
       piorA*100, m1, m2, xp, yp, motivoMeio ? motivoMeio : "invalido");

  // Programa com esses dois pontos e SEM solda entre eles: deslocamento
  // puro, interpolado nas juntas.
  progLimpar();
  const char* m = nullptr;
  progAdicionarPonto(grausParaPassos(J1, A1), grausParaPassos(J2, A2), &m);
  progAdicionarPonto(grausParaPassos(J1, B1), grausParaPassos(J2, B2), &m);
  progDefinirSolda(0, false);

  J1.motor->setCurrentPosition(grausParaPassos(J1, A1));
  J2.motor->setCurrentPosition(grausParaPassos(J2, A2));
  rodarComWeb(5);

  const char* motivo = nullptr;
  const bool aceitou = progIniciar(true, &motivo);   // ensaio, sem arco

  checar(!aceitou, "A07",
         "programa cujo deslocamento atravessa zona proibida deve ser recusado");
  nota("progIniciar(ensaio) -> %s", aceitou ? "ACEITO" : motivo);
  nota("progIniciar() agora valida cada trecho pelo que ele realmente");
  nota("percorre: reta cartesiana quando ha solda, interpolacao nas juntas");
  nota("quando e deslocamento. Validar so as pontas deixava o braco raspar");
  nota("a mesa no meio do caminho.");
  if (aceitou) progParar();
}

// =====================================================================
//  A08 - Emergencia fisica x rearme dos servos
// =====================================================================
static void teste_A08_estop_rearme() {
  secao("A08  Botao de emergencia fisico segurado: da para religar servos?");

  if (!ESTOP_FISICO_INSTALADO) {
    nota("compile com -DESTOP_FISICO_INSTALADO=true para exercitar este ramo");
    nota("(o config.h de producao mantem false ate o botao existir)");
    return;
  }

  reiniciarSistema();
  prepararRoboCalibrado();
  for (int i = 0; i < 3; i++) { enviarComando(CMD_JOG, 1, 1); rodarComWeb(80); }
  nota("antes: servos=%d, braco em movimento=%d",
       (int)servosLigados, (int)motoresEmMovimento());

  // Operador soca o botao (contato NC -> LOW).
  g_pinEntrada[PIN_ESTOP] = LOW;
  rodarComWeb(800);

  checar(!servosLigados && !motoresEmMovimento() && !soldaLigada(), "A08a",
         "a emergencia derruba torque, movimento e arco");
  nota("com o botao acionado: servos=%d, movimento=%d, arco=%d",
       (int)servosLigados, (int)motoresEmMovimento(), (int)soldaLigada());

  // Com o botao AINDA acionado, tenta rearmar e jogar.
  enviarComando(CMD_SERVOS, 1);
  rodarComWeb(50);
  const bool rearmou = servosLigados;
  const long antes = posicaoJ1();
  for (int i = 0; i < 5; i++) { enviarComando(CMD_JOG, 1, 1); rodarComWeb(80); }
  rodarComWeb(300);
  const long depois = posicaoJ1();

  checar(!rearmou && depois == antes, "A08b",
         "com o botao acionado, rearmar servos e jogar devem ser recusados");
  nota("CMD_SERVOS(1) com emergencia ativa: servos=%d | jog: %ld -> %ld passos",
       (int)rearmou, antes, depois);
  nota("Emergencia e condicao de NIVEL. Reagir so na borda deixava um");
  nota("CMD_SERVOS posterior religar o torque com o botao pressionado, e");
  nota("jogAtualizar() nao consultava estop em lugar nenhum.");

  // Solta o botao: o sistema volta a aceitar rearme.
  g_pinEntrada[PIN_ESTOP] = HIGH;
  rodarComWeb(100);
  enviarComando(CMD_SERVOS, 1);
  rodarComWeb(50);
  for (int i = 0; i < 5; i++) { enviarComando(CMD_JOG, 1, 1); rodarComWeb(80); }
  enviarComando(CMD_JOG, 1, 0); rodarComWeb(400);

  checar(servosLigados && posicaoJ1() != depois, "A08c",
         "soltando o botao, o rearme e o jog voltam a funcionar");
  nota("apos soltar: servos=%d, jog moveu para %ld passos",
       (int)servosLigados, posicaoJ1());
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
  nota("definirMensagem() sempre atualiza a mensagem (a interface ve tudo);");
  nota("o eco na serial e que e poupado: repeticao identica nao imprime e");
  nota("ha um piso de 50 ms entre impressoes.");
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

  // Leva o braco a 30 graus e anota onde ele esta na referencia valida.
  J1.motor->setCurrentPosition(grausParaPassos(J1, 30.0f));
  rodarComWeb(5);
  const float antes = passosParaGraus(J1, posicaoJ1());
  nota("calibracao valida: J1 de %.1f a %.1f graus; braco em %.1f graus",
       J1.grausMin, J1.grausMax, antes);

  // O operador comeca uma nova calibracao e define o HOME AQUI...
  enviarComando(CMD_CALIB_INICIAR); rodarComWeb(20);
  enviarComando(CMD_CALIB_CONFIRMAR); rodarComWeb(20);   // zera neste ponto
  nota("depois do HOME da nova calibracao: braco lido como %.1f graus",
       passosParaGraus(J1, posicaoJ1()));

  // ...e desiste.
  enviarComando(CMD_CALIB_CANCELAR); rodarComWeb(30);
  const float depois = passosParaGraus(J1, posicaoJ1());

  checar(J1.calibrada && fabsf(depois - antes) < 0.05f, "A11",
         "cancelar tem que devolver a origem anterior junto com os limites");
  nota("apos cancelar: calibrada=%d, limites %.1f a %.1f, braco em %.1f graus",
       (int)J1.calibrada, J1.grausMin, J1.grausMax, depois);
  nota("calibCancelar() recupera passosMin/passosMax do NVS, que se referem");
  nota("ao zero ANTIGO. Sem desfazer o zerarPosicoes() do CAL_HOME os");
  nota("limites protegeriam a regiao errada, com erro igual a distancia");
  nota("entre os dois zeros (%.0f graus neste cenario).", antes);
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
  nota("trajIniciarReproducao() e progIniciar() exigem servos -- este");
  nota("ultimo tambem no ensaio, que percorre o programa inteiro.");
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
  nota("curso: J1 %.0f..%.0f, J2 %.0f..%.0f graus  |  alcance %.0f mm",
       J1.grausMin, J1.grausMax, J2.grausMin, J2.grausMax, elo1Mm + elo2Mm);

  // ---- 1. O ramo do cotovelo nao pode trocar no meio de um cordao ----
  //
  // Varre retas candidatas e mede, nos DOIS resolvedores, o maior salto
  // de theta2 entre dois pontos consecutivos da interpolacao de 1,5 mm.
  float saltoLivre = 0.0f, saltoTravado = 0.0f;
  bool  trocouLivre = false, trocouTravado = false;
  float sx0 = 0, sy0 = 0;

  for (float y = -300; y <= 300; y += 20)
  for (float x0 = -350; x0 <= 350; x0 += 50) {
    float r1 = 0, r2 = 0;
    const char* m = nullptr;
    if (!resolverXY(x0, y, 0, 0, r1, r2, &m)) continue;
    const bool ramo = ramoCotovelo(r2);

    // (a) resolverXY: reescolhe o ramo a cada passo -- o comportamento antigo.
    float a1 = r1, a2 = r2, ant = r2;
    const int N = (int)(150.0f / PASSO_INTERP_MM);
    for (int k = 1; k <= N; k++) {
      const float a = (float)k / N;
      float t1, t2;
      if (!resolverXY(x0 + 150.0f * a, y, a1, a2, t1, t2, &m)) break;
      const float d = fabsf(t2 - ant);
      if (d > saltoLivre) { saltoLivre = d; sx0 = x0; sy0 = y; }
      if ((ant > 0.5f && t2 < -0.5f) || (ant < -0.5f && t2 > 0.5f)) trocouLivre = true;
      ant = t2; a1 = t1; a2 = t2;
    }

    // (b) resolverXYRamo: ramo travado -- o que a execucao usa agora.
    float b2 = r2; ant = r2;
    for (int k = 1; k <= N; k++) {
      const float a = (float)k / N;
      float t1, t2;
      if (!resolverXYRamo(x0 + 150.0f * a, y, ramo, t1, t2, &m)) break;
      const float d = fabsf(t2 - ant);
      if (d > saltoTravado) saltoTravado = d;
      if ((ant > 0.5f && t2 < -0.5f) || (ant < -0.5f && t2 > 0.5f)) trocouTravado = true;
      ant = t2; (void)t1;
    }
    (void)b2;
  }
  nota("maior salto de theta2 num passo de %.1f mm:", PASSO_INTERP_MM);
  nota("  resolverXY (ramo livre, como era):  %.1f graus, trocou de ramo: %s",
       saltoLivre, trocouLivre ? "SIM" : "nao");
  nota("  resolverXYRamo (ramo travado):      %.1f graus, trocou de ramo: %s",
       saltoTravado, trocouTravado ? "SIM" : "nao");
  nota("  pior reta da varredura: comeca em (%.0f, %.0f) mm", sx0, sy0);
  checar(!trocouTravado, "A13a",
         "com o ramo travado o cotovelo nunca vira de lado dentro do cordao");

  // ---- 2. Salto grande e recusado ANTES de o arco abrir ----
  //
  // Perto de |r| = L1 + L2 a cinematica inversa e mal condicionada: e
  // geometria do braco, nao defeito. O que nao pode e o firmware aceitar
  // o cordao e descobrir isso com o arco aberto.
  const float alc = elo1Mm + elo2Mm;
  Violacao v;
  float r1 = 0, r2 = 0;
  const char* m = nullptr;
  const bool ok0 = resolverXY(alc - 60.0f, 0.0f, 0, 0, r1, r2, &m);
  const bool passa = ok0 && retaCartesianaValida(alc - 60.0f, 0.0f, alc - 1.0f, 0.0f,
                                                 r1, r2, v);
  char det[160]; violacaoTexto(v, det, sizeof(det));
  nota("cordao ate 1 mm do alcance maximo: %s", passa ? "ACEITO" : det);
  checar(ok0 && !passa && v.causa && !strcmp(v.causa, "derivada"), "A13b",
         "cordao que raspa o limite do alcance e recusado pela derivada");

  // ---- 3. Cordao bem condicionado continua passando ----
  const bool ok1 = resolverXY(alc * 0.55f, alc * 0.20f, 0, 0, r1, r2, &m);
  Violacao v2;
  const bool bom = ok1 && retaCartesianaValida(alc * 0.55f, alc * 0.20f,
                                               alc * 0.55f, -alc * 0.20f,
                                               r1, r2, v2);
  if (!bom) { violacaoTexto(v2, det, sizeof(det)); nota("recusado: %s", det); }
  checar(bom, "A13c",
         "cordao longe da borda do alcance continua sendo aceito");

  // ---- 4. O caso da maquina do operador ----
  //
  // Elos de 450 e 400 mm, curso de +/-120 graus nas duas juntas. Existe
  // reta em que o criterio antigo ("o ramo mais barato agora") vira o
  // cotovelo no meio: 24 graus de theta2 num passo de 1,5 mm. Na chapa
  // isso e o braco largar a reta e dar uma volta -- foi o que o operador
  // viu num ziguezague.
  reiniciarSistema();
  prepararRoboCalibrado(120.0f);
  webPost("/api/geometria?l1=450&l2=400");
  rodarComWeb(120);
  protEnvelope = false;

  const float fx0 = -360.0f, fy0 = -770.0f, fx1 = -240.0f;
  float q1 = 0, q2 = 0;
  const char* mm = nullptr;
  const bool alcanca = resolverXY(fx0, fy0, 0, 0, q1, q2, &mm);
  nota("reta (%.0f,%.0f) -> (%.0f,%.0f), a %.0f mm da base (alcance %.0f)",
       (double)fx0, (double)fy0, (double)fx1, (double)fy0,
       sqrt((double)fx0*fx0 + (double)fy0*fy0), (double)(elo1Mm + elo2Mm));

  float saltoL = 0, saltoT = 0;
  bool  flipL = false, flipT = false;
  if (alcanca) {
    const bool ramo = ramoCotovelo(q2);
    float a1 = q1, a2 = q2, ant = q2;
    for (int k = 1; k <= 80; k++) {
      const float a = (float)k / 80.0f;
      float t1, t2;
      if (!resolverXY(fx0 + (fx1 - fx0) * a, fy0, a1, a2, t1, t2, &mm)) break;
      const float d = fabsf(t2 - ant);
      if (d > saltoL) saltoL = d;
      if ((ant > 1 && t2 < -1) || (ant < -1 && t2 > 1)) flipL = true;
      ant = t2; a1 = t1; a2 = t2;
    }
    ant = q2;
    for (int k = 1; k <= 80; k++) {
      const float a = (float)k / 80.0f;
      float t1, t2;
      if (!resolverXYRamo(fx0 + (fx1 - fx0) * a, fy0, ramo, t1, t2, &mm)) break;
      const float d = fabsf(t2 - ant);
      if (d > saltoT) saltoT = d;
      if ((ant > 1 && t2 < -1) || (ant < -1 && t2 > 1)) flipT = true;
      ant = t2; (void)t1;
    }
  }
  nota("  ramo livre  (como era): salto %.1f graus, virou o cotovelo: %s",
       (double)saltoL, flipL ? "SIM" : "nao");
  nota("  ramo travado (agora):   salto %.1f graus, virou o cotovelo: %s",
       (double)saltoT, flipT ? "SIM" : "nao");
  checar(alcanca && flipL && !flipT, "A13d",
         "na maquina do operador o ramo livre virava o cotovelo; o travado nao");

  Violacao v3;
  const bool aceito = alcanca &&
      retaCartesianaValida(fx0, fy0, fx1, fy0, q1, q2, v3);
  violacaoTexto(v3, det, sizeof(det));
  nota("validacao desse cordao: %s", aceito ? "ACEITO" : det);
  checar(!aceito, "A13e",
         "e esse cordao passa a ser recusado antes de o arco abrir");
}

// =====================================================================
//  A14 - Config aplicada pelo core 0 durante movimento
// =====================================================================
static void teste_A14_config_durante_movimento() {
  secao("A14  Configuracao aplicada com o braco em movimento");
  reiniciarSistema();
  prepararRoboCalibrado();

  // Um programa rodando, para o robo NAO estar em modo manual.
  J1.motor->setCurrentPosition(grausParaPassos(J1, 20.0f));
  J2.motor->setCurrentPosition(grausParaPassos(J2, -40.0f));
  rodarComWeb(5);
  enviarComando(CMD_PONTO_GRAVAR); rodarComWeb(10);
  J1.motor->setCurrentPosition(grausParaPassos(J1, 35.0f));
  rodarComWeb(5);
  enviarComando(CMD_PONTO_GRAVAR); rodarComWeb(10);
  enviarComando(CMD_PONTO_SOLDA, 0, 1); rodarComWeb(10);
  enviarComando(CMD_PROG_EXECUTAR, 0); rodarComWeb(1500);
  nota("modo durante o cordao = %d (3 = EXECUTANDO)", (int)modoAtual);

  const uint32_t ppvVivo = J1.passosPorVolta;
  const float    ppgVivo = J1.passosPorGrau;
  const float    t1Vivo  = passosParaGraus(J1, posicaoJ1());

  // Isto e o que handleConfig() faz agora: valida, preenche a area de
  // preparo e enfileira. Nada e escrito nas variaveis vivas aqui.
  prepararConfigPendente();
  configPendente.ppv1 = ppvVivo * 2;
  enviarComando(CMD_APLICAR_CONFIG);
  rodarComWeb(50);

  checar(J1.passosPorVolta == ppvVivo && J1.passosPorGrau == ppgVivo, "A14a",
         "configuracao nao pode ser aplicada com o robo fora do modo manual");
  nota("durante o cordao: ppv=%lu (era %lu), braco em %.1f graus (era %.1f)",
       (unsigned long)J1.passosPorVolta, (unsigned long)ppvVivo,
       passosParaGraus(J1, posicaoJ1()), t1Vivo);

  // Para o programa e tenta de novo, agora em manual.
  enviarComando(CMD_PROG_PARAR); rodarComWeb(600);
  enviarComando(CMD_APLICAR_CONFIG); rodarComWeb(50);

  checar(J1.passosPorVolta == ppvVivo * 2, "A14b",
         "em modo manual a configuracao e aplicada normalmente");
  nota("modo=%d, ppv=%lu, resolucao %.2f -> %.2f pulsos por grau",
       (int)modoAtual, (unsigned long)J1.passosPorVolta, ppgVivo, J1.passosPorGrau);
  nota("Quem escreve nas variaveis vivas e chama recalcularResolucao() e o");
  nota("core 1, dentro de CMD_APLICAR_CONFIG. O handler HTTP so valida os");
  nota("argumentos e preenche configPendente.");
}

// =====================================================================
//  A15 - REGRESSAO: o fluxo completo de solda continua funcionando
// =====================================================================
static void teste_A15_fluxo_completo() {
  secao("A15  REGRESSAO: calibrar, ensinar, ensaiar e soldar de ponta a ponta");
  reiniciarSistema();

  // --- 1. habilitar servos ---
  enviarComando(CMD_SERVOS, 1); rodarComWeb(30);
  checar(servosLigados, "A15a", "servos habilitam");

  // --- 2. calibrar as duas juntas pelo assistente ---
  const long curso = (long)(60.0f * J1.passosPorGrau);
  const bool entrou = rodarAssistente(-curso, +curso, 0, 0, 0, 0);

  checar(entrou && J1.calibrada && J2.calibrada && modoAtual == MODO_MANUAL,
         "A15b", "o assistente de calibracao completa e salva");
  nota("J1 %.1f..%.1f graus, J2 %.1f..%.1f graus",
       J1.grausMin, J1.grausMax, J2.grausMin, J2.grausMax);

  // --- 3. jog manual ---
  const long p0 = posicaoJ1();
  for (int i = 0; i < 5; i++) { enviarComando(CMD_JOG, 1, 1); rodarComWeb(80); }
  enviarComando(CMD_JOG, 1, 0); rodarComWeb(400);
  checar(posicaoJ1() > p0, "A15c", "o jog manual move a junta");
  nota("jog: %ld -> %ld passos (%.1f graus)", p0, posicaoJ1(),
       passosParaGraus(J1, posicaoJ1()));

  // --- 4. ensinar tres pontos: um cordao e um deslocamento ---
  progLimpar();
  const float T1[3] = { 10, 25, 40 };
  const float T2[3] = { -30, -30, -50 };
  for (int i = 0; i < 3; i++) {
    J1.motor->setCurrentPosition(grausParaPassos(J1, T1[i]));
    J2.motor->setCurrentPosition(grausParaPassos(J2, T2[i]));
    rodarComWeb(5);
    enviarComando(CMD_PONTO_GRAVAR); rodarComWeb(20);
  }
  enviarComando(CMD_PONTO_SOLDA, 0, 1); rodarComWeb(20);   // 1->2 com arco
  enviarComando(CMD_PONTO_SOLDA, 1, 0); rodarComWeb(20);   // 2->3 so desloca
  checar(progQuantidade() == 3, "A15d", "os tres pontos sao gravados");
  nota("%u pontos; trecho 1->2 com arco, 2->3 deslocamento",
       (unsigned)progQuantidade());

  // --- 5. ensaio: percurso inteiro sem abrir o arco ---
  g_subidas[PIN_RELE_SOLDA] = 0;
  enviarComando(CMD_PROG_EXECUTAR, 1); rodarComWeb(30);
  const bool ensaioComecou = (modoAtual == MODO_EXECUTANDO);
  uint32_t t = 0;
  while (progRodando() && t < 60000) { rodarComWeb(50); t += 50; }

  checar(ensaioComecou && !progRodando() && g_subidas[PIN_RELE_SOLDA] == 0,
         "A15e", "o ensaio percorre o programa inteiro sem acionar o rele");
  nota("ensaio %s em %.1f s; bordas de subida no rele: %d",
       ensaioComecou ? "concluido" : "NAO INICIOU", t / 1000.0f,
       g_subidas[PIN_RELE_SOLDA]);

  // --- 6. execucao com arco ---
  rodarComWeb(200);
  g_subidas[PIN_RELE_SOLDA] = 0;
  uint32_t msComArco = 0;
  enviarComando(CMD_PROG_EXECUTAR, 0); rodarComWeb(30);
  const bool soldaComecou = (modoAtual == MODO_EXECUTANDO);
  t = 0;
  while (progRodando() && t < 60000) {
    rodarComWeb(10); t += 10;
    if (soldaLigada()) msComArco += 10;
  }

  checar(soldaComecou && !progRodando() && g_subidas[PIN_RELE_SOLDA] == 1 &&
         msComArco > 0 && !soldaLigada(),
         "A15f", "a execucao com arco abre o rele no cordao e fecha no fim");
  nota("execucao %s em %.1f s; rele acionado %d vez(es), %u ms com arco",
       soldaComecou ? "concluida" : "NAO INICIOU", t / 1000.0f,
       g_subidas[PIN_RELE_SOLDA], (unsigned)msComArco);
  nota("estado final do rele: %s, modo: %d",
       soldaLigada() ? "LIGADO" : "desligado", (int)modoAtual);

  // --- 7. gravacao e reproducao de trajetoria ---
  enviarComando(CMD_GRAVAR_INICIAR); rodarComWeb(20);
  for (int i = 0; i < 6; i++) { enviarComando(CMD_JOG, 1, -1); rodarComWeb(60); }
  enviarComando(CMD_JOG, 1, 0); rodarComWeb(300);
  enviarComando(CMD_GRAVAR_PARAR); rodarComWeb(20);
  const uint16_t n = trajPontos();

  enviarComando(CMD_REPRODUZIR); rodarComWeb(50);
  const bool reproduziu = trajReproduzindo();
  t = 0;
  while (trajReproduzindo() && t < 30000) { rodarComWeb(50); t += 50; }

  checar(n >= 2 && reproduziu && !trajReproduzindo(), "A15g",
         "gravacao e reproducao de trajetoria completam");
  nota("%u waypoints gravados; reproducao %s em %.1f s",
       (unsigned)n, reproduziu ? "iniciada e concluida" : "RECUSADA", t / 1000.0f);

  // --- 8. de volta ao manual, pronto para o proximo ciclo ---
  rodarComWeb(100);
  checar(modoAtual == MODO_MANUAL && !soldaLigada() &&
         J1.motor->getAcceleration() == grausPorSegParaHz(J1, J1.aceleracao),
         "A15h", "o sistema volta ao manual com velocidade e aceleracao normais");
  nota("modo=%d, arco=%d, rampa J1 = %lu passos/s2 (configurada %.0f graus/s2)",
       (int)modoAtual, (int)soldaLigada(),
       (unsigned long)J1.motor->getAcceleration(), J1.aceleracao);
}



// =====================================================================
//  A16 - Joystick: velocidade proporcional e zona morta
// =====================================================================
static float velJ1() { return fabsf(J1.motor->getCurrentSpeedInMilliHz() / 1000.0f); }
static float velJ2() { return fabsf(J2.motor->getCurrentSpeedInMilliHz() / 1000.0f); }

static void teste_A16_joystick() {
  secao("A16  Joystick: a velocidade acompanha o quanto o dedo se afastou?");
  reiniciarSistema();
  prepararRoboCalibrado(170.0f);
  protEnvelope = false;

  // Fracao esperada: a zona morta e descontada e o resto e reescalado,
  // para o movimento comecar do zero na borda dela em vez de dar um salto.
  // Esperado em Hz: a fracao do joystick vira graus/s, e cada junta
  // converte com o seu proprio passosPorGrau.
  auto esperado = [](const Junta& j, float f) {
    const float m = fabsf(f);
    if (m < JOY_ZONA_MORTA) return 0.0f;
    const float g = velNormal * ((m - JOY_ZONA_MORTA) / (1.0f - JOY_ZONA_MORTA));
    return g * j.passosPorGrau;
  };
  auto manter = [&](float a, float b, uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 50) {
      enviarComando(CMD_JOG_XY, 0, 0, a, b);
      rodarComWeb(50);
    }
  };

  // --- dentro da zona morta: nada se move ---
  J1.motor->setCurrentPosition(0); J2.motor->setCurrentPosition(0);
  rodarComWeb(5);
  const long p0 = posicaoJ1();
  manter(0.08f, -0.05f, 400);
  const bool parado = (posicaoJ1() == p0) && (velJ1() < 1.0f);
  manter(0, 0, 400);

  checar(parado, "A16a", "dedo tremendo perto do centro nao move nada");
  nota("comando 0,08 / -0,05 (zona morta = %.2f): J1 andou %ld passos",
       JOY_ZONA_MORTA, posicaoJ1() - p0);

  // --- meia forca num eixo so ---
  manter(0.5f, 0.0f, 900);
  const float v50 = velJ1(), v50b = velJ2();
  const float alvo50 = esperado(J1, 0.5f);
  manter(0, 0, 500);

  checar(fabsf(v50 - alvo50) < alvo50 * 0.08f && v50b < 1.0f, "A16b",
         "meio caminho no disco da meia velocidade, e so no eixo pedido");
  nota("comando 0,50: J1 a %.0f Hz (esperado %.0f), J2 a %.0f Hz",
       v50, alvo50, v50b);

  // --- borda do disco: velocidade cheia configurada ---
  // Longe do fim de curso, senao a antecipacao de frenagem entra no meio
  // da medicao e o eixo ja esta desacelerando quando se le a velocidade.
  J1.motor->setCurrentPosition(grausParaPassos(J1, -150.0f));
  rodarComWeb(5);
  manter(1.0f, 0.0f, 1200);
  const float v100 = velJ1();
  manter(0, 0, 500);

  const float alvo100 = velNormal * J1.passosPorGrau;
  checar(fabsf(v100 - alvo100) < alvo100 * 0.05f, "A16c",
         "na borda o eixo anda na velocidade de jog configurada, nao acima");
  nota("comando 1,00: J1 a %.0f Hz = %.1f graus/s (configurado %.1f graus/s)",
       v100, v100 / J1.passosPorGrau, velNormal);

  // --- diagonal: os dois eixos, cada um na sua fracao ---
  J1.motor->setCurrentPosition(0); J2.motor->setCurrentPosition(0);
  rodarComWeb(5);
  manter(0.9f, 0.35f, 1000);
  const float va = velJ1(), vb = velJ2();
  const float ea = esperado(J1, 0.9f), eb = esperado(J2, 0.35f);
  manter(0, 0, 600);

  checar(fabsf(va - ea) < ea * 0.08f && fabsf(vb - eb) < eb * 0.12f &&
         va > vb * 2.0f, "A16d",
         "na diagonal os dois eixos andam juntos, cada um na sua fracao");
  nota("comando 0,90 / 0,35: J1 %.0f Hz (esperado %.0f), J2 %.0f Hz (esperado %.0f)",
       va, ea, vb, eb);
  nota("em graus/s: J1 %.1f, J2 %.1f -- a proporcao e a do dedo, nao a da",
       va / J1.passosPorGrau, vb / J2.passosPorGrau);
  nota("engrenagem de cada eixo.");
  nota("Um comando so para os dois eixos: metade das requisicoes HTTP que");
  nota("mandar /api/jog por eixo, num WebServer que atende uma por vez.");

  // --- soltar o dedo para ---
  const bool parou = (velJ1() < 1.0f && velJ2() < 1.0f);
  checar(parou, "A16e", "soltar o joystick para os dois eixos");
  nota("apos o comando zero: J1 %.0f Hz, J2 %.0f Hz", velJ1(), velJ2());

  // --- sem servos o joystick nao move nada ---
  enviarComando(CMD_SERVOS, 0); rodarComWeb(30);
  const long q0 = posicaoJ1();
  manter(1.0f, 1.0f, 600);
  checar(posicaoJ1() == q0, "A16f",
         "com os servos desligados o joystick tambem e recusado");
  nota("servos=%d, J1 andou %ld passos", (int)servosLigados, posicaoJ1() - q0);
}

// =====================================================================
//  B - CARTAO DE MEMORIA
// =====================================================================
static void prepararCartao() {
  // setup() ja chamou armIniciar(); aqui so se deixa a tarefa rodar os
  // primeiros ciclos, em que ela monta o cartao e abre o log.
  rodarComWeb(30);
}

// Espera a tarefa de cartao terminar o que estava fazendo.
static bool esperarCartao(uint32_t limiteMs = 500) {
  uint32_t t = 0;
  while (armOcupado() && t < limiteMs) { rodarComWeb(5); t += 5; }
  rodarComWeb(20);          // deixa o core 1 consumir o comando postado
  return !armOcupado();
}

static void gravarTresPontos() {
  progLimpar();
  const float T1[3] = { 10, 25, 40 };
  const float T2[3] = { -30, -30, -50 };
  for (int i = 0; i < 3; i++) {
    J1.motor->setCurrentPosition(grausParaPassos(J1, T1[i]));
    J2.motor->setCurrentPosition(grausParaPassos(J2, T2[i]));
    rodarComWeb(5);
    enviarComando(CMD_PONTO_GRAVAR); rodarComWeb(15);
  }
  enviarComando(CMD_PONTO_SOLDA, 0, 1); rodarComWeb(15);
}

// ---------------------------------------------------------------------
static void teste_B01_sem_cartao() {
  secao("B01  Sem cartao no slot, a maquina continua inteira?");
  reiniciarSistema();
  prepararCartao();
  prepararRoboCalibrado();

  // Operador tira o cartao do slot e manda procurar de novo. E o mesmo
  // caminho de quem liga a maquina sem cartao nenhum.
  g_fs.cartaoPresente = false;
  armSolicitar(TAR_MONTAR, "");
  esperarCartao();

  checar(armEstado() == ARM_SEM_CARTAO, "B01a",
         "o firmware percebe que o cartao sumiu, sem travar");
  nota("estado do cartao: %s", armMensagem());

  // Tudo o que nao depende de arquivo tem que seguir funcionando.
  gravarTresPontos();
  const long antes = posicaoJ1();
  for (int i = 0; i < 5; i++) { enviarComando(CMD_JOG, 1, 1); rodarComWeb(80); }
  enviarComando(CMD_JOG, 1, 0); rodarComWeb(400);

  enviarComandoNomeado(CMD_ARQ_SALVAR_PROG, "qualquer");
  rodarComWeb(60);

  checar(progQuantidade() == 3 && posicaoJ1() != antes && modoAtual == MODO_MANUAL,
         "B01b", "jog, pontos e modo seguem normais sem cartao");
  nota("%u pontos gravados, jog %ld -> %ld passos, modo=%d",
       (unsigned)progQuantidade(), antes, posicaoJ1(), (int)modoAtual);
  nota("Pedir para salvar sem cartao apenas recusa: \"%s\"", ultimaMensagem);
  nota("(estado do cartao: \"%s\")", armMensagem());

  // E quando o cartao volta ao slot, ele e reencontrado sozinho.
  g_fs.cartaoPresente = true;
  rodarComWeb(3600);        // a retentativa periodica e de 3 s
  checar(armEstado() == ARM_PRONTO, "B01c",
         "recolocando o cartao, o firmware o encontra sozinho");
  nota("apos %.1f s: %s  (o modulo de 6 pinos nao tem sinal de deteccao,",
       3.6, armMensagem());
  nota("entao a unica forma de perceber e tentar montar de tempos em tempos)");
}

// ---------------------------------------------------------------------
static void teste_B02_programa_ida_e_volta() {
  secao("B02  Programa: salvar no cartao e carregar de volta");
  reiniciarSistema();
  prepararCartao();
  prepararRoboCalibrado();
  gravarTresPontos();

  Ponto original[MAX_PONTOS];
  const uint8_t n0 = progQuantidade();
  memcpy(original, progLista(), (size_t)n0 * sizeof(Ponto));

  enviarComandoNomeado(CMD_ARQ_SALVAR_PROG, "chapa 30x60");
  esperarCartao();
  const bool salvou = (armEstado() == ARM_PRONTO);
  nota("salvar: %s", armMensagem());

  // Apaga o programa da maquina e recarrega do cartao.
  enviarComando(CMD_PROG_LIMPAR); rodarComWeb(20);
  const bool vazio = (progQuantidade() == 0);

  armSolicitar(TAR_CARREGAR_PROG, "chapa 30x60");
  esperarCartao();

  bool igual = (progQuantidade() == n0);
  long piorErro = 0;
  if (igual) {
    for (uint8_t i = 0; i < n0; i++) {
      const long d1 = labs((long)progLista()[i].p1 - original[i].p1);
      const long d2 = labs((long)progLista()[i].p2 - original[i].p2);
      if (d1 > piorErro) piorErro = d1;
      if (d2 > piorErro) piorErro = d2;
      if (progLista()[i].soldaAteProximo != original[i].soldaAteProximo) igual = false;
    }
    if (piorErro > 1) igual = false;
  }

  checar(salvou && vazio && igual, "B02",
         "o programa volta do cartao identico ao que foi salvo");
  nota("%u pontos salvos, programa apagado (%s), %u pontos lidos de volta",
       (unsigned)n0, vazio ? "sim" : "nao", (unsigned)progQuantidade());
  nota("maior diferenca em passos: %ld (arredondamento de graus para passos)",
       piorErro);
  nota("carregar: %s", ultimaMensagem);
}

// ---------------------------------------------------------------------
static void teste_B03_programa_em_graus() {
  secao("B03  Programa gravado em graus sobrevive a troca de resolucao?");
  reiniciarSistema();
  prepararCartao();
  prepararRoboCalibrado();
  gravarTresPontos();

  const float t1Original = passosParaGraus(J1, progLista()[0].p1);
  enviarComandoNomeado(CMD_ARQ_SALVAR_PROG, "portatil");
  esperarCartao();

  // Troca a engrenagem eletronica: mesma maquina, outra resolucao.
  prepararConfigPendente();
  configPendente.ppv1 = J1.passosPorVolta * 2;
  configPendente.ppv2 = J2.passosPorVolta * 2;
  enviarComando(CMD_APLICAR_CONFIG);
  rodarComWeb(40);
  nota("resolucao J1: %.2f -> %.2f pulsos por grau", 27.78, J1.passosPorGrau);

  // Recalibra o curso para a nova escala e recarrega.
  J1.passosMin = grausParaPassos(J1, -90.0f); J1.passosMax = grausParaPassos(J1, 90.0f);
  J2.passosMin = grausParaPassos(J2, -90.0f); J2.passosMax = grausParaPassos(J2, 90.0f);
  recalcularResolucao();
  enviarComando(CMD_PROG_LIMPAR); rodarComWeb(20);

  armSolicitar(TAR_CARREGAR_PROG, "portatil");
  esperarCartao();

  const float t1Depois = (progQuantidade() > 0)
                       ? passosParaGraus(J1, progLista()[0].p1) : -999.0f;

  checar(progQuantidade() == 3 && fabsf(t1Depois - t1Original) < 0.02f, "B03",
         "o ponto volta no mesmo ANGULO, nao no mesmo numero de passos");
  nota("ponto 1: %.3f graus ao salvar, %.3f graus ao carregar", t1Original, t1Depois);
  nota("em passos: %ld antes da troca, %ld depois",
       (long)grausParaPassos(J1, t1Original) / 2, (long)progLista()[0].p1);
  nota("Guardar passos amarraria o arquivo a engrenagem eletronica em uso;");
  nota("em graus ele continua valendo e da para escrever um no computador.");
}

// ---------------------------------------------------------------------
static void teste_B04_arquivo_corrompido() {
  secao("B04  Arquivo corrompido derruba o programa que esta na maquina?");
  reiniciarSistema();
  prepararCartao();
  prepararRoboCalibrado();
  gravarTresPontos();
  const uint8_t n0 = progQuantidade();

  // Um arquivo qualquer que nao e um programa.
  g_fs.arquivos["/prog/lixo.prg"] = "isto aqui nao e um programa\nbla bla\n";
  armSolicitar(TAR_CARREGAR_PROG, "lixo");
  esperarCartao();
  const bool recusou1 = (armEstado() == ARM_ERRO);
  const uint8_t n1 = progQuantidade();

  // Cabecalho certo, ponto fora do curso calibrado.
  g_fs.arquivos["/prog/fora.prg"] =
      "ROBOCNC-PROG 1\nelos=200.000,200.000\npontos=2\n"
      "10.0000 -30.0000 1\n"
      "500.0000 -30.0000 0\n";
  armSolicitar(TAR_CARREGAR_PROG, "fora");
  esperarCartao();
  const uint8_t n2 = progQuantidade();

  checar(recusou1 && n1 == n0 && n2 == n0, "B04",
         "arquivo invalido e recusado sem apagar o programa da maquina");
  nota("programa na maquina: %u pontos antes, %u apos o lixo, %u apos o ponto fora",
       (unsigned)n0, (unsigned)n1, (unsigned)n2);
  nota("recusa 1: %s", armMensagem());
  nota("recusa 2: %s", ultimaMensagem);
  nota("A tarefa de SD le para uma area de troca e so entao pede ao core 1");
  nota("que aplique; o core 1 valida cada ponto antes de tocar no vivo.");
}

// ---------------------------------------------------------------------
static void teste_B05_nome_de_arquivo() {
  secao("B05  Nome de arquivo vindo de HTTP pode escapar da pasta?");

  struct { const char* nome; bool esperado; } casos[] = {
    { "chapa 30x60",           true  },
    { "flange-4_lados",        true  },
    { "../../boot",            false },
    { "/etc/passwd",           false },
    { "prog/../../log/s0001",  false },
    { "com.ponto",             false },
    { "",                      false },
    { " comeca com espaco",    false },
    { "nome_absurdamente_grande_que_nao_cabe", false },
  };
  bool todos = true;
  for (auto& c : casos) {
    const bool r = armNomeValido(c.nome);
    if (r != c.esperado) todos = false;
    nota("%-42s %s  (esperado %s)", c.nome[0] ? c.nome : "(vazio)",
         r ? "aceito " : "recusado", c.esperado ? "aceito" : "recusado");
  }
  checar(todos, "B05", "so nomes simples passam; nada de travessia de diretorio");
  nota("armNomeValido() aceita apenas [A-Za-z0-9 _-] e ate %u caracteres.",
       (unsigned)MAX_NOME_ARQ);
}

// ---------------------------------------------------------------------
static void teste_B06_trajetoria_binaria() {
  secao("B06  Trajetoria: gravar, salvar em binario e recarregar");
  reiniciarSistema();
  prepararCartao();
  prepararRoboCalibrado();

  enviarComando(CMD_GRAVAR_INICIAR); rodarComWeb(20);
  for (int i = 0; i < 8; i++) { enviarComando(CMD_JOG, 1, 1); rodarComWeb(60); }
  enviarComando(CMD_JOG, 1, 0); rodarComWeb(200);
  enviarComando(CMD_GRAVAR_PARAR); rodarComWeb(20);

  const uint16_t n0 = trajPontos();
  const uint32_t dur0 = trajDuracaoMs();
  Waypoint copia[64];
  const uint16_t k = (n0 < 64) ? n0 : 64;
  memcpy(copia, trajBuffer(), (size_t)k * sizeof(Waypoint));

  enviarComandoNomeado(CMD_ARQ_SALVAR_TRAJ, "contorno"); esperarCartao();
  const bool salvou = (armEstado() == ARM_PRONTO);
  const uint32_t bytes = (uint32_t)g_fs.arquivos["/traj/contorno.trj"].size();

  enviarComando(CMD_TRAJ_LIMPAR); rodarComWeb(20);
  const bool limpou = (trajPontos() == 0);

  enviarComandoNomeado(CMD_ARQ_CARREGAR_TRAJ, "contorno"); esperarCartao();

  bool igual = (trajPontos() == n0) && (trajDuracaoMs() == dur0);
  if (igual) igual = (memcmp(trajBuffer(), copia, (size_t)k * sizeof(Waypoint)) == 0);

  checar(salvou && limpou && igual, "B06",
         "a trajetoria volta byte a byte igual");
  nota("%u waypoints, %.2f s, %lu bytes no cartao (%u por ponto)",
       (unsigned)n0, dur0 / 1000.0f, (unsigned long)bytes,
       (unsigned)sizeof(Waypoint));
  nota("mesmos %u pontos apos recarregar: %s",
       (unsigned)trajPontos(), igual ? "sim" : "NAO");
  nota("O buffer vivo e emprestado para a tarefa de SD (trajEmprestar), entao");
  nota("nao existe copia de %u kB na RAM so para gravar.",
       (unsigned)(MAX_WAYPOINTS * sizeof(Waypoint) / 1024));

  // Buffer emprestado: gravar tem que ser recusado enquanto isso.
  trajEmprestar();
  const bool recusou = !trajIniciarGravacao();
  trajDevolver();
  checar(recusou, "B06b",
         "com o buffer emprestado ao cartao, gravar e recusado");
  nota("trajIniciarGravacao() com emprestimo ativo: %s",
       recusou ? "recusado" : "ACEITO (corromperia a leitura)");
}

// ---------------------------------------------------------------------
static void teste_B07_config_backup() {
  secao("B07  Ajustes: backup no cartao e restauracao");
  reiniciarSistema();
  prepararCartao();
  prepararRoboCalibrado();

  // Configuracao "boa", salva em arquivo.
  prepararConfigPendente();
  configPendente.velNormal = 4321;
  configPendente.elo1      = 234.0f;
  configPendente.protEnvelope = true;
  enviarComando(CMD_APLICAR_CONFIG); rodarComWeb(40);
  enviarComandoNomeado(CMD_ARQ_SALVAR_CONFIG, "backup-oficina"); esperarCartao();
  const bool salvou = (armEstado() == ARM_PRONTO);
  nota("salvo: velN=%lu, elo1=%.0f, protecao de mesa=%d",
       (unsigned long)velNormal, elo1Mm, (int)protEnvelope);

  // Alguem baguncou tudo.
  prepararConfigPendente();
  configPendente.velNormal = 900;
  configPendente.elo1      = 100.0f;
  configPendente.protEnvelope = false;
  enviarComando(CMD_APLICAR_CONFIG); rodarComWeb(40);
  nota("bagunçado: velN=%lu, elo1=%.0f, protecao de mesa=%d",
       (unsigned long)velNormal, elo1Mm, (int)protEnvelope);

  armSolicitar(TAR_CARREGAR_CONFIG, "backup-oficina");
  esperarCartao();
  rodarComWeb(40);

  checar(salvou && velNormal == 4321 && fabsf(elo1Mm - 234.0f) < 0.01f &&
         protEnvelope, "B07a", "a restauracao devolve os ajustes salvos");
  nota("restaurado: velN=%lu, elo1=%.0f, protecao de mesa=%d",
       (unsigned long)velNormal, elo1Mm, (int)protEnvelope);
  nota("Carregar preenche a area de preparo e enfileira CMD_APLICAR_CONFIG:");
  nota("o mesmo caminho do POST /api/config, com as mesmas validacoes.");

  // Arquivo com valor absurdo nao pode passar so por vir do cartao.
  g_fs.arquivos["/cfg/torto.cfg"] =
      "ROBOCNC-CFG 1\nvelN=9999999\nl1=200\n";
  const uint32_t antes = velNormal;
  armSolicitar(TAR_CARREGAR_CONFIG, "torto");
  esperarCartao();
  rodarComWeb(40);
  checar(velNormal == antes, "B07b",
         "configuracao com valor fora de faixa e recusada");
  nota("velN pedido = 9999999 (teto do driver = %lu); velN atual = %lu",
       (unsigned long)FREQ_PULSO_MAX_HZ, (unsigned long)velNormal);
  nota("recusa: %s", armMensagem());
}

// ---------------------------------------------------------------------
static void teste_B08_arquivo_durante_execucao() {
  secao("B08  Mexer em arquivo com o programa rodando");
  reiniciarSistema();
  prepararCartao();
  prepararRoboCalibrado();
  gravarTresPontos();

  enviarComandoNomeado(CMD_ARQ_SALVAR_PROG, "base"); esperarCartao();
  enviarComando(CMD_PROG_EXECUTAR, 0); rodarComWeb(1200);
  const bool rodando = (modoAtual == MODO_EXECUTANDO);
  const uint8_t n0 = progQuantidade();

  // Carregar outro programa no meio da solda seria trocar o chao sob os pes.
  armSolicitar(TAR_CARREGAR_PROG, "base");
  esperarCartao();

  checar(rodando && progQuantidade() == n0 && modoAtual == MODO_EXECUTANDO,
         "B08", "carregar arquivo nao troca o programa em execucao");
  nota("modo=%d, %u pontos (inalterado), mensagem: %s",
       (int)modoAtual, (unsigned)progQuantidade(), ultimaMensagem);
  nota("A tarefa de SD le o arquivo, mas CMD_ARQ_APLICAR_PROG exige modo");
  nota("manual: a area de troca fica preenchida e o vivo nao e tocado.");
  enviarComando(CMD_PROG_PARAR); rodarComWeb(600);
}

// ---------------------------------------------------------------------
static void teste_B09_registro_de_eventos() {
  secao("B09  Registro de eventos no cartao");
  reiniciarSistema();
  prepararCartao();
  prepararRoboCalibrado();
  gravarTresPontos();

  enviarComando(CMD_PROG_EXECUTAR, 1); rodarComWeb(200);
  enviarComando(CMD_PROG_PARAR); rodarComWeb(600);

  // Perde a conexao: o supervisor registra.
  rodar(4000);
  rodarComWeb(1500);        // deixa o escoamento periodico rodar

  std::string log;
  for (auto& a : g_fs.arquivos)
    if (a.first.rfind("/log/", 0) == 0) log = a.second;

  const bool temCabecalho = log.find("ms_desde_boot") != std::string::npos;
  const bool temInicio    = log.find("sistema iniciado") != std::string::npos;
  const bool temConexao   = log.find("conexao perdida") != std::string::npos;

  checar(temCabecalho && temInicio && temConexao, "B09",
         "eventos de seguranca chegam ao arquivo de log");
  nota("arquivo de log com %u bytes", (unsigned)log.size());
  for (size_t i = 0, l = 0; i < log.size() && l < 6; l++) {
    const size_t f = log.find('\n', i);
    nota("  %s", log.substr(i, (f == std::string::npos ? log.size() : f) - i).c_str());
    if (f == std::string::npos) break;
    i = f + 1;
  }
  nota("logEvento() so enfileira (timeout zero) e quem grava e a tarefa do");
  nota("core 0: registrar nunca atrasa o laco de controle.");
}


// =====================================================================
//  C - MENSAGENS DE RECUSA
// =====================================================================
static void teste_C01_mensagens_de_recusa() {
  secao("C01  A recusa diz ONDE, qual junta e quanto faltou?");
  reiniciarSistema();
  prepararRoboCalibrado();     // curso +/-90

  // Dois pontos folgados (t2 = 80 num curso de 90). A reta cartesiana
  // entre eles passa perto da base, e um braco 2R precisa dobrar o
  // cotovelo para alcancar perto: o MEIO do cordao exige muito mais
  // curso do que qualquer uma das pontas.
  progLimpar();
  const float A1 = -60, A2 = 80, B1 = 60, B2 = 80;
  J1.motor->setCurrentPosition(grausParaPassos(J1, A1));
  J2.motor->setCurrentPosition(grausParaPassos(J2, A2));
  rodarComWeb(5);
  const char* ma = nullptr;
  const bool okA = progAdicionarPonto(posicaoJ1(), posicaoJ2(), &ma);
  J1.motor->setCurrentPosition(grausParaPassos(J1, B1));
  J2.motor->setCurrentPosition(grausParaPassos(J2, B2));
  rodarComWeb(5);
  const char* mb = nullptr;
  const bool okB = progAdicionarPonto(posicaoJ1(), posicaoJ2(), &mb);
  progDefinirSolda(0, true);

  checar(okA && okB, "C01a", "as duas pontas do cordao sao aceitas");
  nota("ponto 1 t=(%.0f, %.0f) e ponto 2 t=(%.0f, %.0f), curso %.0f..%.0f",
       A1, A2, B1, B2, J1.grausMin, J1.grausMax);

  // A conferencia por trecho tem de reprovar, com texto util.
  char aviso[176] = "";
  const bool trechoOk = progConferirTrecho(0, aviso, sizeof(aviso));
  const bool util = !trechoOk &&
                    strstr(aviso, "cordao 1->2") &&
                    strstr(aviso, "junta 2") &&
                    strstr(aviso, "% do trecho") &&
                    strstr(aviso, "curso vai ate");
  checar(util, "C01b",
         "a recusa nomeia o trecho, a junta, o ponto do percurso e o limite");
  nota("\"%s\"", aviso);
  nota("Antes a frase era so \"junta 2 no fim do curso calibrado\": o");
  nota("operador olhava dois pontos a 80 graus num curso de 90 e concluia");
  nota("que a maquina estava errada.");

  // E tem de relatar a PIOR violacao, nao a primeira.
  float pior = 0.0f;
  const char* p = strstr(aviso, "ir a ");
  if (p) pior = (float)atof(p + 5);
  checar(pior > 120.0f, "C01c",
         "relata a pior exigencia do percurso, nao a primeira que aparece");
  nota("valor relatado: %.1f graus. A primeira violacao da reta e de 89.8", pior);
  nota("graus -- relatar ela faria o operador abrir o limite em 1 grau e");
  nota("nao entender por que continua recusado.");

  // progIniciar tem de devolver a mesma frase, nunca vazia.
  const char* motivo = nullptr;
  const bool iniciou = progIniciar(true, &motivo);
  checar(!iniciou && motivo && strstr(motivo, "cordao 1->2"), "C01d",
         "progIniciar recusa com a mesma frase, nunca em silencio");
  nota("progIniciar: %s", motivo ? motivo : "(sem motivo!)");
  if (iniciou) progParar();
}

static void teste_C02_braco_fora_da_area() {
  secao("C02  Braco parado fora da area util: a recusa aponta para ele?");
  reiniciarSistema();
  prepararRoboCalibrado();

  progLimpar();
  const float T1[2] = { -20, 20 }, T2[2] = { 30, 30 };
  for (int i = 0; i < 2; i++) {
    J1.motor->setCurrentPosition(grausParaPassos(J1, T1[i]));
    J2.motor->setCurrentPosition(grausParaPassos(J2, T2[i]));
    rodarComWeb(5);
    const char* m = nullptr;
    progAdicionarPonto(posicaoJ1(), posicaoJ2(), &m);
  }
  progDefinirSolda(0, true);

  // O braco vai parar fora do curso util (acontece ao mudar resolucao,
  // refazer calibracao ou perder passo).
  J1.motor->setCurrentPosition(grausParaPassos(J1, J1.grausMax + 2.0f));
  rodarComWeb(5);

  const char* motivo = nullptr;
  const bool iniciou = progIniciar(true, &motivo);
  const bool aponta = !iniciou && motivo &&
                      strstr(motivo, "braco esta parado fora") &&
                      strstr(motivo, "jog");
  checar(aponta, "C02",
         "a recusa diz que o problema e a posicao atual, e o que fazer");
  nota("braco em t1 = %.1f (curso util ate %.1f)",
       passosParaGraus(J1, posicaoJ1()), J1.grausMax - MARGEM_LIMITE_GRAUS);
  nota("\"%s\"", motivo ? motivo : "(sem motivo!)");
  nota("Antes saia \"junta 1 no fim do curso calibrado\" e o operador ia");
  nota("procurar o defeito nos pontos, que estavam todos certos.");
  if (iniciou) progParar();
}

static void teste_C03_cordao_bom_passa() {
  secao("C03  Cordao que cabe no curso continua passando");
  reiniciarSistema();
  prepararRoboCalibrado();

  // Cordao curto, longe da base: nao exige dobrar o cotovelo.
  progLimpar();
  const float T1[2] = { 10, 22 }, T2[2] = { -25, -25 };
  for (int i = 0; i < 2; i++) {
    J1.motor->setCurrentPosition(grausParaPassos(J1, T1[i]));
    J2.motor->setCurrentPosition(grausParaPassos(J2, T2[i]));
    rodarComWeb(5);
    const char* m = nullptr;
    progAdicionarPonto(posicaoJ1(), posicaoJ2(), &m);
  }
  progDefinirSolda(0, true);
  J1.motor->setCurrentPosition(grausParaPassos(J1, T1[0]));
  J2.motor->setCurrentPosition(grausParaPassos(J2, T2[0]));
  rodarComWeb(5);

  char aviso[176] = "";
  const bool trechoOk = progConferirTrecho(0, aviso, sizeof(aviso));
  const char* motivo = nullptr;
  const bool iniciou = progIniciar(true, &motivo);

  float x0, y0, x1, y1, xc, yc;
  cinematicaDireta(T1[0], T2[0], xc, yc, x0, y0);
  cinematicaDireta(T1[1], T2[1], xc, yc, x1, y1);
  checar(trechoOk && iniciou, "C03",
         "cordao percorrivel nao e recusado pela validacao nova");
  nota("cordao (%.0f, %.0f) -> (%.0f, %.0f) mm, %.0f mm", x0, y0, x1, y1,
       sqrtf((x1-x0)*(x1-x0) + (y1-y0)*(y1-y0)));
  nota("conferencia do trecho: %s | progIniciar: %s",
       trechoOk ? "ok" : aviso, iniciou ? "aceito" : (motivo ? motivo : "?"));
  if (iniciou) progParar();
}


// =====================================================================
//  E - O SOFTWARE CONCORDA COM O BRACO DE VERDADE?
// =====================================================================

// ---------------------------------------------------------------------
static void teste_E01_aferir_resolucao() {
  secao("E01  Resolucao digitada errada: o curso medido corrige?");
  reiniciarSistema();
  enviarComando(CMD_SERVOS, 1); rodarComWeb(30);

  // Cenario real: o operador digitou a engrenagem eletronica certa
  // (10000) mas esqueceu a reducao do redutor, que e 2:1. A maquina
  // acredita em metade dos pulsos por grau que existem de fato.
  prepararConfigPendente();
  configPendente.ppv1 = 10000; configPendente.red1 = 1.0f;
  configPendente.ppv2 = 10000; configPendente.red2 = 1.0f;
  enviarComando(CMD_APLICAR_CONFIG); rodarComWeb(40);
  const float ppgAntes = J1.passosPorGrau;

  // O braco varre 5556 pulsos de curso. Com a reducao 2:1 de verdade,
  // isso sao 100 graus reais -- mas a maquina vai calcular 200.
  const long meio = 2778;
  rodarAssistente(-meio, +meio, 0, 0, 0, 0);   // sem aferir
  const float cursoCru = J1.grausMax - J1.grausMin;

  checar(fabsf(cursoCru - 200.0f) < 1.0f, "E01a",
         "sem aferir, a maquina reporta o curso pela resolucao digitada");
  nota("%.2f pulsos/grau digitados -> curso calculado de %.1f graus",
       ppgAntes, cursoCru);
  nota("O braco de verdade girou 100. A conta esta certa; o numero que");
  nota("entrou nela e que estava errado.");

  // Agora o operador mede com transferidor e informa os 100 graus reais.
  rodarAssistente(-meio, +meio, 0, 0, 100.0f, 100.0f);

  const float ppgDepois = J1.passosPorGrau;
  const float cursoAferido = J1.grausMax - J1.grausMin;

  checar(fabsf(cursoAferido - 100.0f) < 0.1f &&
         fabsf(ppgDepois - 55.56f) < 0.2f, "E01b",
         "informado o curso real, a resolucao e recalculada pelos pulsos contados");
  nota("curso informado 100 graus, %ld pulsos contados", 2 * meio);
  nota("resolucao: %.2f -> %.2f pulsos/grau; curso agora %.2f graus",
       ppgAntes, ppgDepois, cursoAferido);

  // A reducao no painel de ajustes tem de explicar a nova resolucao,
  // senao um recalculo posterior desfaz a afericao.
  const float ppgRecalc = (J1.passosPorVolta * J1.reducao) / 360.0f;
  checar(fabsf(J1.reducao - 2.0f) < 0.01f &&
         fabsf(ppgRecalc - ppgDepois) < 0.05f, "E01c",
         "a reducao mostrada passa a explicar a resolucao aferida");
  nota("reducao reescrita para %.4f : 1  (o redutor real e 2:1)", J1.reducao);
  nota("recalculo a partir dela: %.2f pulsos/grau -- bate com a aferida",
       ppgRecalc);

  // E o braco parado no meio do curso tem de ler o angulo certo.
  J1.motor->setCurrentPosition(0); rodarComWeb(5);
  checar(fabsf(passosParaGraus(J1, posicaoJ1())) < 0.01f, "E01d",
         "no meio do curso o software le o mesmo angulo que o braco esta");
  nota("pulso 0 -> %.3f graus", passosParaGraus(J1, posicaoJ1()));
}

// ---------------------------------------------------------------------
static void teste_E02_angulo_da_referencia() {
  secao("E02  Referencia gravada fora do zero: o desenho acompanha?");
  reiniciarSistema();
  enviarComando(CMD_SERVOS, 1); rodarComWeb(30);

  // A cinematica chama de zero o braco esticado apontando para +X. Aqui
  // a referencia e gravada com a junta 1 a 30 graus e a 2 a -15.
  const long meio = (long)(60.0f * ((10000 * 1.0f) / 360.0f));
  rodarAssistente(-meio, +meio, 30.0f, -15.0f, 0, 0);

  J1.motor->setCurrentPosition(0);
  J2.motor->setCurrentPosition(0);
  rodarComWeb(5);
  const float t1 = passosParaGraus(J1, posicaoJ1());
  const float t2 = passosParaGraus(J2, posicaoJ2());

  checar(fabsf(t1 - 30.0f) < 0.05f && fabsf(t2 + 15.0f) < 0.05f, "E02a",
         "na referencia o software le os angulos que o operador declarou");
  nota("contador em zero pulso -> software le %.2f / %.2f graus", t1, t2);

  // Ida e volta exata: sem isso todo ponto gravado escorregaria.
  const long p = grausParaPassos(J1, 47.5f);
  checar(fabsf(passosParaGraus(J1, p) - 47.5f) < 0.02f, "E02b",
         "graus -> passos -> graus fecha, com o offset no meio");
  nota("47.5 graus -> %ld passos -> %.3f graus", p, passosParaGraus(J1, p));

  // Os limites deslocam junto.
  checar(fabsf(J1.grausMin - (-30.0f)) < 0.5f &&
         fabsf(J1.grausMax - (90.0f)) < 0.5f, "E02c",
         "o curso calibrado e reportado nos angulos reais da maquina");
  nota("curso da junta 1: %.1f a %.1f graus (60 para cada lado de 30)",
       J1.grausMin, J1.grausMax);

  // E a ponta desenhada tem de sair onde a postura real coloca.
  float xc, yc, xp, yp;
  cinematicaDireta(t1, t2, xc, yc, xp, yp);
  float xr, yr, xe, ye;
  cinematicaDireta(30.0f, -15.0f, xr, yr, xe, ye);
  checar(fabsf(xp - xe) < 0.1f && fabsf(yp - ye) < 0.1f, "E02d",
         "a ponta no desenho cai onde a postura real do braco coloca");
  nota("ponta desenhada (%.0f, %.0f) mm; postura real 30/-15 da (%.0f, %.0f)",
       xp, yp, xe, ye);
}

// ---------------------------------------------------------------------
static void teste_E03_sem_informar_nada() {
  secao("E03  Quem nao preencher nada tem o comportamento de antes");
  reiniciarSistema();
  enviarComando(CMD_SERVOS, 1); rodarComWeb(30);

  const float ppgAntes = J1.passosPorGrau;
  const float redAntes = J1.reducao;
  const long meio = (long)(45.0f * ppgAntes);

  rodarAssistente(-meio, +meio, 0, 0, 0, 0);

  checar(J1.calibrada && J2.calibrada &&
         fabsf(J1.passosPorGrau - ppgAntes) < 0.001f &&
         fabsf(J1.reducao - redAntes) < 0.001f &&
         fabsf(J1.grausHome) < 0.001f &&
         fabsf(J1.grausMin + 45.0f) < 0.5f &&
         fabsf(J1.grausMax - 45.0f) < 0.5f, "E03",
         "campos em branco: resolucao, offset e curso como sempre foram");
  nota("resolucao %.3f (era %.3f), offset %.1f, curso %.1f a %.1f",
       J1.passosPorGrau, ppgAntes, J1.grausHome, J1.grausMin, J1.grausMax);

  // Valor absurdo tem de ser ignorado, nao aceito.
  rodarAssistente(-meio, +meio, 0, 0, 0.3f, 0.0f);
  checar(fabsf(J1.passosPorGrau - ppgAntes) < 0.001f, "E03b",
         "curso informado menor que o minimo e ignorado, nao aplicado");
  nota("informado 0.3 grau (minimo %.0f): resolucao segue em %.3f",
       CURSO_MINIMO_GRAUS, J1.passosPorGrau);
}


// =====================================================================
//  F - MODO DE INSTALACAO, APAGAR CALIBRACAO E SENTIDO DOS EIXOS
// =====================================================================
static void teste_F01_jog_livre_sem_calibracao() {
  secao("F01  Sem calibracao o jog trava? (era o que impedia calibrar)");
  reiniciarSistema();
  enviarComando(CMD_SERVOS, 1); rodarComWeb(30);

  // Situacao real: a resolucao digitada esta MUITO menor que a de
  // verdade, entao poucos pulsos ja leem um angulo enorme. Com a
  // protecao de dobra ativa sobre esse angulo, o jog era recusado antes
  // de o operador conseguir chegar em qualquer limite.
  prepararConfigPendente();
  configPendente.ppv1 = 200; configPendente.red1 = 1.0f;   // 0,56 pulso/grau
  configPendente.ppv2 = 200; configPendente.red2 = 1.0f;
  enviarComando(CMD_APLICAR_CONFIG); rodarComWeb(40);
  nota("resolucao digitada: %.2f pulsos por grau (protecao de dobra em %.0f)",
       J1.passosPorGrau, 180.0f - folgaDobra);

  J1.motor->setCurrentPosition(0);
  J2.motor->setCurrentPosition(grausParaPassos(J2, 170.0f));  // "dobrado"
  rodarComWeb(10);
  nota("junta 2 lida em %.0f graus -- alem do limite de dobra",
       passosParaGraus(J2, posicaoJ2()));

  const bool valida = posturaValida(passosParaGraus(J1, posicaoJ1()),
                                    passosParaGraus(J2, posicaoJ2()), nullptr);
  const long antes = posicaoJ1();
  for (int i = 0; i < 8; i++) { enviarComando(CMD_JOG, 1, 1); rodarComWeb(70); }
  enviarComando(CMD_JOG, 1, 0); rodarComWeb(500);

  checar(valida && posicaoJ1() != antes, "F01a",
         "sem calibracao o jog e livre: e assim que se chega aos limites");
  nota("postura considerada valida: %s | jog andou %ld passos",
       valida ? "sim" : "NAO", posicaoJ1() - antes);
  nota("Sem referencia, \"graus\" e pulso dividido por um numero digitado.");
  nota("Aplicar dobra ou envelope sobre isso travava justamente o");
  nota("assistente que existe para estabelecer a referencia.");

  // O assistente tem de rodar inteiro nessas condicoes.
  const long meio = (long)(60.0f * J1.passosPorGrau);
  rodarAssistente(-meio, +meio, 0, 0, 0, 0);
  checar(J1.calibrada && J2.calibrada, "F01b",
         "o assistente de calibracao completa mesmo com resolucao torta");
  nota("calibrado: J1 %.1f..%.1f, J2 %.1f..%.1f graus",
       J1.grausMin, J1.grausMax, J2.grausMin, J2.grausMax);

  // E depois de calibrado as protecoes voltam a valer.
  const bool protegeDepois = !posturaValida(0.0f, 170.0f, nullptr);
  checar(protegeDepois, "F01c",
         "com a calibracao pronta as protecoes voltam a agir");
  nota("postura (0, 170) apos calibrar: %s",
       protegeDepois ? "recusada" : "ACEITA");
}

// ---------------------------------------------------------------------
static void teste_F02_apagar_calibracao() {
  secao("F02  Apagar a calibracao gravada");
  reiniciarSistema();
  prepararRoboCalibrado();
  J1.grausHome = 30.0f; J2.grausHome = -15.0f;
  recalcularResolucao();
  salvarConfiguracoes();
  const float ppgAntes = J1.passosPorGrau;
  const float redAntes = J1.reducao;
  nota("antes: calibrada=%d, curso J1 %.1f..%.1f, referencia em %.0f graus",
       (int)J1.calibrada, J1.grausMin, J1.grausMax, J1.grausHome);

  enviarComando(CMD_CALIB_APAGAR); rodarComWeb(60);

  checar(!J1.calibrada && !J2.calibrada &&
         J1.passosMin == 0 && J1.passosMax == 0 &&
         fabsf(J1.grausHome) < 0.001f, "F02a",
         "apagar limpa limites, referencia e a marca de calibrada");
  nota("depois: calibrada=%d, limites %ld..%ld, referencia %.1f",
       (int)J1.calibrada, J1.passosMin, J1.passosMax, J1.grausHome);
  nota("mensagem: %s", ultimaMensagem);

  // A resolucao descreve a mecanica, nao a medicao: nao se apaga junto.
  checar(fabsf(J1.passosPorGrau - ppgAntes) < 0.001f &&
         fabsf(J1.reducao - redAntes) < 0.001f, "F02b",
         "a resolucao dos eixos sobrevive: ela e da mecanica, nao da medicao");
  nota("resolucao segue em %.3f pulsos/grau, reducao %.3f",
       J1.passosPorGrau, J1.reducao);

  // Tem de ficar apagada depois de religar.
  carregarConfiguracoes();
  checar(!J1.calibrada && !J2.calibrada, "F02c",
         "apagada tambem no NVS: nao volta no proximo boot");
  nota("apos recarregar o NVS: calibrada=%d", (int)J1.calibrada);

  // E o robo volta ao modo de instalacao, com jog livre.
  enviarComando(CMD_SERVOS, 1); rodarComWeb(30);
  const long antes = posicaoJ1();
  for (int i = 0; i < 6; i++) { enviarComando(CMD_JOG, 1, 1); rodarComWeb(70); }
  enviarComando(CMD_JOG, 1, 0); rodarComWeb(400);
  // Com dois pontos gravados, a recusa tem de ser pela calibracao --
  // e nao por falta de pontos.
  progLimpar();
  for (int i = 0; i < 2; i++) {
    J1.motor->setCurrentPosition(grausParaPassos(J1, 10.0f + 15.0f * i));
    rodarComWeb(5);
    const char* mp = nullptr;
    progAdicionarPonto(posicaoJ1(), posicaoJ2(), &mp);
  }
  const char* m = nullptr;
  const bool progRecusado = !progIniciar(true, &m);
  checar(posicaoJ1() != antes && progRecusado && m && strstr(m, "calibre"),
         "F02d", "modo de instalacao: jog livre, mas programa recusado");
  nota("jog andou %ld passos | %u pontos gravados", posicaoJ1() - antes,
       (unsigned)progQuantidade());
  nota("programa: %s", m ? m : "ACEITO");
}

// ---------------------------------------------------------------------
static void teste_F03_sentido_do_eixo() {
  secao("F03  Braco indo para um lado e o desenho para o outro");
  reiniciarSistema();
  prepararRoboCalibrado();

  const bool padrao = J1.motor->dirSobe;
  checar(padrao && !J1.inverterDir, "F03a",
         "de fabrica o eixo conta subindo com o DIR em nivel alto");
  nota("inverterDir=%d, contagem sobe com DIR alto: %s",
       (int)J1.inverterDir, padrao ? "sim" : "nao");

  // Fiacao do DIR trocada: marca-se na tela em vez de trocar fio.
  prepararConfigPendente();
  configPendente.inv1 = true;
  enviarComando(CMD_APLICAR_CONFIG); rodarComWeb(60);

  checar(J1.inverterDir && !J1.motor->dirSobe && !J2.inverterDir, "F03b",
         "marcado, o sentido e reaplicado no gerador de pulso da junta certa");
  nota("J1: inverterDir=%d, conta subindo com DIR alto: %s",
       (int)J1.inverterDir, J1.motor->dirSobe ? "sim" : "nao");
  nota("J2 intacta: inverterDir=%d", (int)J2.inverterDir);
  nota("Nenhuma calibracao conserta sinal trocado: o erro nao e de escala.");

  // Persistir e reaplicar no boot.
  salvarConfiguracoes();
  J1.inverterDir = false;
  carregarConfiguracoes();
  aplicarSentido();
  checar(J1.inverterDir && !J1.motor->dirSobe, "F03c",
         "o sentido sobrevive ao NVS e e reaplicado ao ligar");
  nota("apos recarregar: inverterDir=%d, dirSobe=%s",
       (int)J1.inverterDir, J1.motor->dirSobe ? "sim" : "nao");

  // Restaurar padroes desfaz.
  enviarComando(CMD_RESTAURAR_PADROES); rodarComWeb(60);
  checar(!J1.inverterDir && J1.motor->dirSobe, "F03d",
         "restaurar padroes devolve o sentido normal");
  nota("apos restaurar: inverterDir=%d", (int)J1.inverterDir);
}


// =====================================================================
//  G - AS DUAS JUNTAS ANDAM NA MESMA VELOCIDADE?
// =====================================================================
static void teste_G01_velocidade_igual_entre_juntas() {
  secao("G01  Engrenagens diferentes, mesma velocidade angular");
  reiniciarSistema();
  prepararRoboCalibrado(170.0f);
  protEnvelope = false;

  // A maquina do operador: reducao 16,5 na junta 1 e 4 na junta 2, com
  // 10000 pulsos por volta nas duas. Sao 4,1 vezes de diferenca em
  // pulsos por grau.
  prepararConfigPendente();
  configPendente.ppv1 = 10000; configPendente.red1 = 16.5f;
  configPendente.ppv2 = 10000; configPendente.red2 = 4.0f;
  configPendente.velNormal = 20.0f;
  enviarComando(CMD_APLICAR_CONFIG); rodarComWeb(60);
  J1.passosMin = grausParaPassos(J1, -170.0f); J1.passosMax = grausParaPassos(J1, 170.0f);
  J2.passosMin = grausParaPassos(J2, -170.0f); J2.passosMax = grausParaPassos(J2, 170.0f);
  recalcularResolucao();
  nota("J1 %.1f pulsos/grau, J2 %.1f -- razao de %.1f vezes",
       J1.passosPorGrau, J2.passosPorGrau, J1.passosPorGrau / J2.passosPorGrau);

  // Joystick no talo nos dois eixos.
  J1.motor->setCurrentPosition(grausParaPassos(J1, -150.0f));
  J2.motor->setCurrentPosition(grausParaPassos(J2, -150.0f));
  rodarComWeb(10);
  const long p1 = posicaoJ1(), p2 = posicaoJ2();
  const uint32_t t0 = g_millis;
  for (uint32_t t = 0; t < 2000; t += 50) {
    enviarComando(CMD_JOG_XY, 0, 0, 1.0f, 1.0f);
    rodarComWeb(50);
  }
  const float dt = (g_millis - t0) / 1000.0f;
  const float g1 = (passosParaGraus(J1, posicaoJ1()) - passosParaGraus(J1, p1)) / dt;
  const float g2 = (passosParaGraus(J2, posicaoJ2()) - passosParaGraus(J2, p2)) / dt;
  enviarComando(CMD_JOG_XY, 0, 0, 0, 0); rodarComWeb(600);

  checar(fabsf(g1 - g2) < velNormal * 0.10f, "G01a",
         "as duas juntas percorrem o mesmo angulo por segundo");
  nota("em %.1f s: J1 andou %.1f °/s, J2 andou %.1f °/s (configurado %.1f)",
       dt, g1, g2, velNormal);
  nota("Em Hz, os mesmos 3000 pulsos/s dariam 6,5 °/s na junta 1 e 27 na");
  nota("junta 2 -- era exatamente a queixa de um braco mais rapido que o outro.");

  // Em pulsos elas continuam bem diferentes, e tem de ser.
  const float hz1 = fabsf(J1.motor->getCurrentSpeedInMilliHz() / 1000.0f);
  const float hz2 = fabsf(J2.motor->getCurrentSpeedInMilliHz() / 1000.0f);
  nota("(paradas agora: %.0f e %.0f Hz)", hz1, hz2);

  // Movimento coordenado: as duas tem de CHEGAR juntas.
  J1.motor->setCurrentPosition(0); J2.motor->setCurrentPosition(0);
  rodarComWeb(10);
  moverCoordenado(grausParaPassos(J1, 60.0f), grausParaPassos(J2, 20.0f), velAuto);
  uint32_t tj1 = 0, tj2 = 0, t = 0;
  while ((J1.motor->isRunning() || J2.motor->isRunning()) && t < 20000) {
    rodarComWeb(10); t += 10;
    if (J1.motor->isRunning()) tj1 = t;
    if (J2.motor->isRunning()) tj2 = t;
  }
  checar(tj1 > 0 && labs((long)tj1 - (long)tj2) < 200, "G01b",
         "num movimento coordenado as duas juntas chegam juntas");
  nota("60 graus na junta 1 e 20 na junta 2 a %.1f °/s: J1 parou em %u ms,",
       velAuto, (unsigned)tj1);
  nota("J2 em %u ms (diferenca de %ld ms)", (unsigned)tj2,
       labs((long)tj1 - (long)tj2));
  nota("Quem manda no tempo e a junta com mais GRAUS a percorrer -- antes");
  nota("era a com mais passos, que com engrenagens diferentes e outra coisa.");

  // Rampa tambem angular.
  const uint32_t a1 = J1.motor->getAcceleration(), a2 = J2.motor->getAcceleration();
  aplicarAceleracao();
  const float ga1 = J1.motor->getAcceleration() / J1.passosPorGrau;
  const float ga2 = J2.motor->getAcceleration() / J2.passosPorGrau;
  checar(fabsf(ga1 - ga2) < 1.0f, "G01c",
         "a rampa tambem e a mesma nas duas, em graus por segundo ao quadrado");
  nota("rampa: J1 %.0f °/s² (%lu passos/s²), J2 %.0f °/s² (%lu passos/s²)",
       ga1, (unsigned long)J1.motor->getAcceleration(),
       ga2, (unsigned long)J2.motor->getAcceleration());
  nota("Rampa desigual e causa classica de perda de passo no eixo mais leve.");
  (void)a1; (void)a2;
}

// =====================================================================
//  H - Camada HTTP.
//
//  Ate agora servidor_web.cpp so era compilado, nunca executado. Os dois
//  defeitos que o operador sentiu na mao ("gravar ponto nao faz nada", "a
//  velocidade de cordao nao salva") moravam exatamente ali. O mock de
//  WebServer despacha a rota de verdade, no mesmo processo.
// =====================================================================
static void teste_H01_velocidade_de_cordao() {
  secao("H01  A velocidade de cordao salva quando muda na tela?");
  reiniciarSistema();
  prepararRoboCalibrado();

  const float antes = velCordaoMmS;
  // Exatamente o que a interface manda ao apertar "Salvar ajustes".
  const int cod = webPost("/api/config?velN=20&velP=2&velA=12&velCordao=7.5"
                          "&acel1=60&acel2=60&ppv1=4000&red1=16.5"
                          "&ppv2=4000&red2=4&suav=120");
  rodarComWeb(120);
  nota("antes %.1f mm/s, pedido 7.5, agora %.1f (HTTP %d)",
       (double)antes, (double)velCordaoMmS, cod);
  checar(cod == 200 && fabsf(velCordaoMmS - 7.5f) < 0.01f, "H01a",
         "POST /api/config?velCordao=7.5 muda a velocidade do cordao");

  // Fica gravada: religar nao pode devolver o valor velho.
  const float gravado = (g_nvs.f.count("velCmm") ? g_nvs.f["velCmm"] : -1.0f);
  nota("no NVS: %.1f mm/s", (double)gravado);
  checar(fabsf(gravado - 7.5f) < 0.01f, "H01b",
         "o valor vai para a memoria nao volatil");

  // O nome antigo continua funcionando para quem tiver a pagina em cache.
  webPost("/api/config?velC=4.25");
  rodarComWeb(120);
  checar(fabsf(velCordaoMmS - 4.25f) < 0.01f, "H01c",
         "o nome antigo velC ainda e aceito");

  // E o pedido sem o parametro nenhum nao pode zerar o que estava.
  webPost("/api/config?velN=20");
  rodarComWeb(120);
  checar(fabsf(velCordaoMmS - 4.25f) < 0.01f, "H01d",
         "pedido sem velCordao preserva o valor atual");
}

static void teste_H02_suavidade_da_partida() {
  secao("H02  Suavidade da partida chega nos geradores de pulso?");
  reiniciarSistema();
  prepararRoboCalibrado();

  const int cod = webPost("/api/config?suav=90");
  rodarComWeb(120);
  nota("suavidade %u; setLinearAcceleration J1=%u J2=%u",
       (unsigned)suavidadePartida,
       (unsigned)J1.motor->suavidade,
       (unsigned)J2.motor->suavidade);
  checar(cod == 200 && suavidadePartida == 90, "H02a",
         "POST /api/config?suav=90 grava a suavidade");
  checar(J1.motor->suavidade == 90 &&
         J2.motor->suavidade == 90, "H02b",
         "as duas juntas recebem a rampa em S");
  checar(g_nvs.u.count("suav") && g_nvs.u["suav"] == 90, "H02c",
         "a suavidade sobrevive ao desligamento");

  // Zero e valido: quem quiser a rampa antiga, reta, poe zero.
  webPost("/api/config?suav=0");
  rodarComWeb(120);
  checar(suavidadePartida == 0 && J1.motor->suavidade == 0,
         "H02d", "zero desliga a rampa em S sem recusar o pedido");
}

static void teste_H03_zerar_na_posicao() {
  secao("H03  Zerar a maquina na posicao atual");
  reiniciarSistema();
  prepararRoboCalibrado();

  // Braco deslocado do zero, como fica depois de perder passo.
  J1.motor->setCurrentPosition(1234);
  J2.motor->setCurrentPosition(-567);
  rodarComWeb(20);
  nota("antes: J1 %ld passos (%.1f°), J2 %ld passos (%.1f°)",
       posicaoJ1(), (double)passosParaGraus(J1, posicaoJ1()),
       posicaoJ2(), (double)passosParaGraus(J2, posicaoJ2()));

  const int cod = webPost("/api/referenciar");
  rodarComWeb(200);
  nota("depois: J1 %ld passos, J2 %ld passos -- \"%s\"",
       posicaoJ1(), posicaoJ2(), ultimaMensagem);
  checar(cod == 200 && posicaoJ1() == 0 && posicaoJ2() == 0, "H03a",
         "POST /api/referenciar zera a contagem dos dois eixos");
  checar(modoAtual == MODO_MANUAL, "H03b",
         "o robo continua em manual, sem entrar em assistente");

  // Fora do manual nao: reescrever a contagem debaixo do gerador de pulso
  // e o jeito mais rapido de mandar o braco para o batente.
  enviarComando(CMD_GRAVAR_INICIAR);
  rodarComWeb(40);
  const int cod2 = webPost("/api/referenciar");
  nota("em modo %d: HTTP %d -- \"%s\"", (int)modoAtual, cod2, webCorpo());
  checar(modoAtual != MODO_MANUAL && cod2 == 400, "H03c",
         "recusado, com motivo, quando o robo nao esta em manual");
}

static void teste_H04_aferir_reducao() {
  secao("H04  Aferir a reducao mecanica pelo movimento real");
  reiniciarSistema();
  prepararRoboCalibrado();

  // O operador declarou 20:1, mas a maquina tem 16,5:1. O sintoma e o
  // braco andar menos do que a tela mostra.
  webPost("/api/config?ppv1=4000&red1=20");
  rodarComWeb(120);
  const float ppgAntes = J1.passosPorGrau;
  const float ppg2Antes = J2.passosPorGrau;

  checar(webPost("/api/aferir/marcar?j=1") == 200, "H04a",
         "POST /api/aferir/marcar aceita a junta 1");
  rodarComWeb(120);

  // Gira o eixo: 45 graus pela conta ERRADA de 20:1.
  const long pulsos = (long)(45.0f * ppgAntes);
  J1.motor->setCurrentPosition(posicaoJ1() + pulsos);
  rodarComWeb(20);
  nota("contados %ld pulsos desde a marca", aferirPassosDesde(1));
  checar(aferirPassosDesde(1) == pulsos, "H04b",
         "os pulsos desde a marca aparecem para a interface");

  // Transferidor no eixo: ele andou 54,5 graus de verdade.
  const int cod = webPost("/api/aferir/aplicar?j=1&g=54.5");
  rodarComWeb(200);
  const float esperado = (float)pulsos / 54.5f;
  nota("reducao %.2f -> %.2f (%.1f -> %.1f pulsos/grau) -- \"%s\"",
       20.0, (double)J1.reducao, (double)ppgAntes, (double)J1.passosPorGrau,
       ultimaMensagem);
  checar(cod == 200 && fabsf(J1.passosPorGrau - esperado) < 0.5f, "H04c",
         "a resolucao passa a ser pulsos contados / graus medidos");
  checar(fabsf(J1.reducao - 16.5f) < 0.2f, "H04d",
         "a reducao mecanica exibida vira a real (16,5:1)");
  checar(fabsf(J2.passosPorGrau - ppg2Antes) < 0.01f, "H04e",
         "a outra junta nao e tocada");

  // Sem marca nao ha o que aferir.
  reiniciarSistema();
  prepararRoboCalibrado();
  const float ppg2 = J2.passosPorGrau;
  webPost("/api/aferir/aplicar?j=2&g=30");
  rodarComWeb(120);
  nota("sem marcar antes: \"%s\"", ultimaMensagem);
  checar(strstr(ultimaMensagem, "arque") != nullptr &&
         fabsf(J2.passosPorGrau - ppg2) < 0.01f, "H04f",
         "sem marca o sistema diz o que faltou em vez de gravar lixo");

  // Angulo perto de zero mandaria a resolucao para o infinito.
  const int codG = webPost("/api/aferir/aplicar?j=1&g=0");
  nota("g=0: HTTP %d -- \"%s\"", codG, webCorpo());
  checar(codG == 400 &&
         webPost("/api/aferir/aplicar?j=1&g=-40") == 400 &&
         webPost("/api/aferir/aplicar?j=9&g=40")  == 400, "H04g",
         "angulo invalido e junta inexistente sao recusados na porta");
}

static void teste_H05_desenho_vira_programa() {
  secao("H05  Desenhar na mesa vira programa de pontos");
  reiniciarSistema();
  prepararRoboCalibrado();

  const float alc = elo1Mm + elo2Mm;
  // Os pontos do traco saem da cinematica DIRETA de posturas validas: um
  // XY escolhido no olho pode cair fora do curso de +/-90 graus e o teste
  // reprovaria por causa do teste, nao do firmware.
  auto pontoDe = [&](float t1, float t2, float& x, float& y) {
    float xc, yc; cinematicaDireta(t1, t2, xc, yc, x, y);
  };
  float ax, ay, bx, by, cx2, cy2;
  pontoDe(20.0f, 30.0f, ax, ay);
  pontoDe(28.0f, 30.0f, bx, by);
  pontoDe(36.0f, 30.0f, cx2, cy2);
  char traco[256];
  snprintf(traco, sizeof(traco), "%.1f,%.1f;%.1f,%.1f;%.1f,%.1f",
           (double)ax, (double)ay, (double)bx, (double)by,
           (double)cx2, (double)cy2);
  nota("traco pedido: (%.0f,%.0f) (%.0f,%.0f) (%.0f,%.0f)",
       (double)ax, (double)ay, (double)bx, (double)by,
       (double)cx2, (double)cy2);

  const int cod = webPost("/api/prog/desenho?solda=1", traco);
  rodarComWeb(200);
  nota("HTTP %d, %u pontos -- \"%s\"", cod, (unsigned)progQuantidade(),
       ultimaMensagem);
  checar(cod == 200 && progQuantidade() == 3, "H05a",
         "o traco vira pontos do programa");
  checar(progQuantidade() == 3 && progLista()[0].soldaAteProximo == 1 &&
         progLista()[1].soldaAteProximo == 1, "H05b",
         "com solda=1 os trechos saem marcados como cordao");
  checar(progQuantidade() == 3 && progLista()[2].soldaAteProximo == 0,
         "H05c", "o ultimo ponto nao abre arco: depois dele nao ha trecho");

  // A ponta cai onde o dedo passou.
  if (progQuantidade() == 3) {
    const float t1 = passosParaGraus(J1, progLista()[1].p1);
    const float t2 = passosParaGraus(J2, progLista()[1].p2);
    float xc, yc, x, y; cinematicaDireta(t1, t2, xc, yc, x, y);
    nota("ponto 2 pedido em (%.1f, %.1f), gravado em (%.1f, %.1f)",
         (double)bx, (double)by, (double)x, (double)y);
    checar(fabsf(x - bx) < 1.0f && fabsf(y - by) < 1.0f, "H05d",
           "o ponto gravado cai onde o dedo passou, em milimetros de chapa");
  } else {
    checar(false, "H05d", "o ponto gravado cai onde o dedo passou");
  }

  // Traco fora de alcance: recusa dizendo QUAL ponto, e o programa que ja
  // estava na maquina nao pode ser apagado por isso.
  const uint8_t antes = progQuantidade();
  char longe[128];
  snprintf(longe, sizeof(longe), "%.1f,%.1f;0,%.0f",
           (double)ax, (double)ay, (double)(alc * 3.0f));
  const int cod2 = webPost("/api/prog/desenho", longe);
  rodarComWeb(120);
  nota("fora de alcance: HTTP %d -- \"%s\"", cod2, webCorpo());
  checar(cod2 == 400 && strstr(webCorpo(), "ponto 2") != nullptr, "H05e",
         "traco fora de alcance e recusado apontando o ponto");
  checar(progQuantidade() == antes, "H05f",
         "o programa que estava na maquina continua intacto");

  // Traco maior que o programa comporta.
  std::string grande;
  for (int i = 0; i < MAX_PONTOS + 3; i++) {
    float x, y; pontoDe(20.0f + i * 0.4f, 30.0f, x, y);
    char b[40];
    snprintf(b, sizeof(b), "%s%.1f,%.1f", i ? ";" : "", (double)x, (double)y);
    grande += b;
  }
  const int cod3 = webPost("/api/prog/desenho", grande.c_str());
  rodarComWeb(120);
  nota("%d pontos: HTTP %d -- \"%s\"", MAX_PONTOS + 3, cod3, webCorpo());
  checar(cod3 == 400, "H05g",
         "traco com mais pontos do que o programa comporta e recusado");

  // Um traco de um ponto so nao e caminho nenhum.
  char um[40];
  snprintf(um, sizeof(um), "%.1f,%.1f", (double)ax, (double)ay);
  checar(webPost("/api/prog/desenho", um) == 400, "H05h",
         "traco de um ponto so e recusado");
  checar(webPost("/api/prog/desenho", "isto nao e um traco") == 400, "H05i",
         "corpo mal formado e recusado em vez de virar pontos aleatorios");

  // Terceiro campo por ponto: e assim que um DXF com varios contornos
  // vira cordao em cada contorno e deslocamento entre eles.
  float dx, dy, ex, ey;
  pontoDe(20.0f, 30.0f, dx, dy);
  pontoDe(30.0f, 30.0f, ex, ey);
  char porPonto[200];
  snprintf(porPonto, sizeof(porPonto), "%.1f,%.1f,1;%.1f,%.1f,0;%.1f,%.1f,1;%.1f,%.1f,0",
           (double)ax, (double)ay, (double)dx, (double)dy,
           (double)ex, (double)ey, (double)bx, (double)by);
  const int cod4 = webPost("/api/prog/desenho?solda=0", porPonto);
  rodarComWeb(200);
  nota("por ponto: HTTP %d, %u pontos -- \"%s\"", cod4,
       (unsigned)progQuantidade(), cod4 == 200 ? ultimaMensagem : webCorpo());
  checar(cod4 == 200 && progQuantidade() == 4, "H05j",
         "cada ponto pode trazer o proprio estado de arco");
  checar(progQuantidade() == 4 &&
         progLista()[0].soldaAteProximo == 1 &&
         progLista()[1].soldaAteProximo == 0 &&
         progLista()[2].soldaAteProximo == 1, "H05k",
         "os trechos saem exatamente como o desenho pediu, nao todos iguais");
  checar(progQuantidade() == 4 && progLista()[3].soldaAteProximo == 0, "H05l",
         "o ultimo ponto continua sem arco mesmo se o campo vier 1");
}

static void teste_H06_rotas_da_interface() {
  secao("H06  Toda rota existe? (botao mudo e o defeito mais caro)");
  reiniciarSistema();

  // Rota inexistente tem de responder 404, senao um erro de digitacao na
  // pagina vira "o botao nao faz nada" sem nenhum sinal.
  const int cod = webPost("/api/nao/existe");
  nota("POST /api/nao/existe -> HTTP %d", cod);
  checar(cod == 404, "H06a", "rota desconhecida responde 404");

  // GET numa rota que so aceita POST tambem nao pode passar em silencio.
  checar(webGet("/api/parar") == 404, "H06b",
         "metodo errado nao cai num handler qualquer");

  // A conferencia rota-a-rota contra a pagina fica em conferir_rotas.py,
  // que le pagina_web.h: aqui so se garante o comportamento do 404.
  nota("a lista completa e conferida por testes/conferir_rotas.py");
}

// =====================================================================
//  I - Ziguezague: o cordao segue a polilinha ou foge dela?
//
//  Queixa do operador: com varios pontos em ziguezague o braco "fugiu da
//  posicao e fez uma circunferencia" em vez de reta. Aqui o percurso da
//  PONTA e amostrado a cada milissegundo e comparado com a polilinha que
//  o programa deveria desenhar.
// =====================================================================
struct PontoXY { float x, y; };

// Distancia de um ponto ao segmento AB.
static float distSegmento(float px, float py, float ax, float ay,
                          float bx, float by) {
  const float dx = bx - ax, dy = by - ay;
  const float L2 = dx * dx + dy * dy;
  float u = (L2 > 1e-6f) ? ((px - ax) * dx + (py - ay) * dy) / L2 : 0.0f;
  if (u < 0.0f) u = 0.0f;
  if (u > 1.0f) u = 1.0f;
  const float qx = ax + dx * u, qy = ay + dy * u;
  return sqrtf((px - qx) * (px - qx) + (py - qy) * (py - qy));
}
static float distPolilinha(float px, float py, const PontoXY* v, int n) {
  float melhor = 1e9f;
  for (int i = 0; i + 1 < n; i++) {
    const float d = distSegmento(px, py, v[i].x, v[i].y, v[i + 1].x, v[i + 1].y);
    if (d < melhor) melhor = d;
  }
  return melhor;
}
static void pontaAgora(float& x, float& y) {
  float xc, yc;
  cinematicaDireta(passosParaGraus(J1, posicaoJ1()),
                   passosParaGraus(J2, posicaoJ2()), xc, yc, x, y);
}

// Monta um ziguezague de 'n' vertices e o carrega como programa pela
// mesma rota que a interface usa para o desenho a mao livre.
static int montarZiguezague(PontoXY* v, int n, float x0, float passoX,
                            float yBaixo, float yAlto, bool solda) {
  std::string corpo;
  for (int i = 0; i < n; i++) {
    v[i].x = x0 + passoX * i;
    v[i].y = (i % 2) ? yAlto : yBaixo;
    char b[40];
    snprintf(b, sizeof(b), "%s%.1f,%.1f", i ? ";" : "",
             (double)v[i].x, (double)v[i].y);
    corpo += b;
  }
  return webPost(solda ? "/api/prog/desenho?solda=1"
                       : "/api/prog/desenho?solda=0", corpo.c_str());
}

static void teste_I01_ziguezague_reto() {
  secao("I01  Ziguezague: a ponta segue a polilinha?");
  reiniciarSistema();
  prepararRoboCalibrado(120.0f);
  // A maquina do operador: elos de 450 e 400 mm.
  webPost("/api/geometria?l1=450&l2=400");
  rodarComWeb(120);

  const int N = 11;
  PontoXY v[N];
  const int cod = montarZiguezague(v, N, 350.0f, 30.0f, 380.0f, 460.0f, true);
  rodarComWeb(200);
  nota("HTTP %d (%s), %u pontos carregados -- \"%s\"", cod, webCorpo(),
       (unsigned)progQuantidade(), ultimaMensagem);
  checar(cod == 200 && progQuantidade() == N, "I01a",
         "o ziguezague de 11 vertices vira programa");
  if (progQuantidade() != N) return;

  // Executa com arco (foi assim que o operador viu o defeito).
  // Pela fila de comandos, como a interface faz: progIniciar() sozinho
  // nao poe o robo em MODO_EXECUTANDO, e sem isso o loop nunca chama
  // progAtualizar().
  enviarComando(CMD_PROG_EXECUTAR, 0);
  rodarComWeb(40);
  const bool partiu = progRodando() && modoAtual == MODO_EXECUTANDO;
  nota("modo=%d progRodando=%d -- \"%s\"", (int)modoAtual,
       (int)progRodando(), ultimaMensagem);
  checar(partiu, "I01b", "o programa e aceito para execucao");
  if (!partiu) return;

  float pior = 0.0f, piorX = 0, piorY = 0;
  uint32_t piorMs = 0;
  double soma = 0; uint32_t amostras = 0;
  // Espera a aproximacao terminar antes de medir: o caminho ate o ponto 1
  // e deslocamento, nao cordao, e sai curvo de proposito.
  uint32_t t = 0;
  while (progRodando() && progIndiceAtual() == 0 && t < 40000) {
    if (t % 200 == 0) registrarContatoOperador();
    rodar(1); t++;
    if (getenv("VERBOSE") && t % 2000 == 0) {
      float x, y; pontaAgora(x, y);
      nota("  t=%6u fase=%u idx=%u ponta=(%.1f,%.1f) mov=%d solda=%d lib=%d",
           t, progFaseTeste(), progIndiceAtual(), (double)x, (double)y,
           (int)motoresEmMovimento(), (int)soldaLigada(), (int)movimentoLiberado);
    }
  }
  while (progRodando() && t < 400000) {
    if (t % 200 == 0) registrarContatoOperador();
    rodar(1); t++;
    float x, y; pontaAgora(x, y);
    const float d = distPolilinha(x, y, v, N);
    soma += d; amostras++;
    if (d > pior) { pior = d; piorX = x; piorY = y; piorMs = t; }
  }
  nota("percurso de %u ms, %u amostras", t, amostras);
  nota("desvio maximo da polilinha: %.1f mm em (%.0f, %.0f) aos %u ms",
       (double)pior, (double)piorX, (double)piorY, piorMs);
  nota("desvio medio: %.2f mm", amostras ? soma / amostras : 0.0);
  nota("fim: \"%s\"", ultimaMensagem);

  // 1,5 mm e o passo da interpolacao cartesiana: o cordao nao pode sair
  // mais do que isso da reta que o operador desenhou.
  checar(pior < 2.0f, "I01c",
         "a ponta nunca sai mais de 2 mm da polilinha desenhada");
  checar(!progRodando() && strstr(ultimaMensagem, "concluido") != nullptr,
         "I01d", "o programa chega ao fim em vez de abortar no meio");
}

static void teste_I02_ziguezague_na_borda() {
  secao("I02  Ziguezague raspando o limite do alcance");
  reiniciarSistema();
  prepararRoboCalibrado(120.0f);
  webPost("/api/geometria?l1=450&l2=400");
  rodarComWeb(120);
  const float alc = elo1Mm + elo2Mm;

  // Ziguezague com os vertices a 2 mm do alcance maximo. E ali que a
  // cinematica inversa amplifica milimetros em dezenas de graus, e onde
  // o criterio antigo virava o cotovelo no meio do cordao.
  //
  // A invariante nao e "tem de ser recusado": e "nunca executado torto".
  // Recusar com motivo serve; executar seguindo a reta serve; sair da
  // reta com o arco aberto, nao.
  const int N = 7;
  PontoXY v[N];
  std::string corpo;
  for (int i = 0; i < N; i++) {
    const float ang = (-25.0f + 8.0f * i) * (float)M_PI / 180.0f;
    const float r   = (i % 2) ? (alc - 2.0f) : (alc - 60.0f);
    v[i].x = r * cosf(ang); v[i].y = r * sinf(ang);
    char b[40];
    snprintf(b, sizeof(b), "%s%.1f,%.1f", i ? ";" : "", (double)v[i].x, (double)v[i].y);
    corpo += b;
  }
  const int cod = webPost("/api/prog/desenho?solda=1", corpo.c_str());
  rodarComWeb(200);

  if (cod != 200) {
    nota("recusado ja na carga: \"%s\"", webCorpo());
    checar(true, "I02a", "o desenho na borda e recusado com motivo, antes de soldar");
    return;
  }
  nota("carregado: \"%s\"", ultimaMensagem);

  enviarComando(CMD_PROG_EXECUTAR, 0);
  rodarComWeb(60);
  if (!progRodando()) {
    nota("execucao recusada: \"%s\"", ultimaMensagem);
    checar(!soldaLigada(), "I02a",
           "recusado antes de o arco abrir, com motivo na tela");
    return;
  }

  nota("execucao aceita -- entao ela tem de seguir a reta");
  uint32_t t = 0;
  while (progRodando() && progIndiceAtual() == 0 && t < 60000) {
    if (t % 200 == 0) registrarContatoOperador();
    rodar(1); t++;
  }
  float pior = 0.0f, piorX = 0, piorY = 0;
  while (progRodando() && t < 400000) {
    if (t % 200 == 0) registrarContatoOperador();
    rodar(1); t++;
    float x, y; pontaAgora(x, y);
    const float d = distPolilinha(x, y, v, N);
    if (d > pior) { pior = d; piorX = x; piorY = y; }
  }
  nota("desvio maximo: %.1f mm em (%.0f, %.0f); fim: \"%s\"",
       (double)pior, (double)piorX, (double)piorY, ultimaMensagem);
  checar(pior < 2.0f, "I02a",
         "aceito o cordao na borda, a ponta ainda segue a reta");
  checar(!progRodando(), "I02b", "o programa termina em vez de travar");
}

static void teste_I03_velocidade_entre_trechos() {
  secao("I03  A velocidade programada nao pode ficar velha entre trechos");
  reiniciarSistema();
  prepararRoboCalibrado(120.0f);
  webPost("/api/geometria?l1=450&l2=400");
  rodarComWeb(120);

  // Deslocamento (moverCoordenado) e depois seguimento de setpoint com o
  // MESMO numero de Hz que ficou no cache antigo. Com o cache privado
  // dentro de seguirSetpoint, esta segunda chamada nao reprogramava nada
  // e o trecho corria na velocidade do deslocamento.
  const uint32_t alvo = 1234;
  moverCoordenado(posicaoJ1() + 4000, posicaoJ2(), 12.0f);
  rodar(5);
  const uint32_t depoisDoDeslocamento = J1.motor->velHz;
  seguirSetpoint(posicaoJ1() + 100, posicaoJ2(), alvo, alvo);
  rodar(1);
  nota("apos moverCoordenado: %u Hz; apos seguirSetpoint(%u): %u Hz",
       depoisDoDeslocamento, alvo, J1.motor->velHz);
  checar(J1.motor->velHz == alvo, "I03a",
         "seguirSetpoint reprograma mesmo depois de outro tipo de movimento");

  // E continua nao reprogramando a toa: repetir o mesmo valor nao pode
  // mandar setSpeedInHz de novo (refazer a rampa mil vezes por segundo
  // aparece como aspereza no cordao).
  J1.motor->velHz = 999999;             // marca: se reprogramar, muda
  seguirSetpoint(posicaoJ1() + 200, posicaoJ2(), alvo, alvo);
  rodar(1);
  checar(J1.motor->velHz == 999999, "I03b",
         "repetir a mesma velocidade nao reprograma o gerador");
}

// =====================================================================
//  J - Rede: Wi-Fi proprio, e so isso
// =====================================================================
// =====================================================================
//  L - Encoder por Modbus
// =====================================================================
static void prepararEncoder(uint16_t reg, bool baixaPrimeiro, int32_t posicao) {
  encoderPendente = configEncoder;
  encoderPendente.ativo         = true;
  encoderPendente.baud          = 19200;
  encoderPendente.paridade      = 0;
  encoderPendente.funcao        = 3;
  encoderPendente.periodoMs     = ENC_PERIODO_MIN_MS;
  encoderPendente.trintaEDois   = true;
  encoderPendente.baixaPrimeiro = baixaPrimeiro;
  encoderPendente.id[0]  = 1;   encoderPendente.id[1]  = 2;
  encoderPendente.reg[0] = reg; encoderPendente.reg[1] = 0;   // junta 2 nao ligada
  encoderPendente.contagensPorVolta[0] = 10000.0f;
  encoderPendente.contagensPorVolta[1] = 10000.0f;
  enviarComando(CMD_APLICAR_ENCODER);
  rodarComWeb(60);

  g_uart.escravo[0] = EscravoModbus{};
  g_uart.escravo[0].id = 1;
  g_uart.escravo[0].funcao = 3;
  g_uart.escravo[0].regBase = reg;
  g_uart.escravo[0].baixaPrimeiro = baixaPrimeiro;
  g_uart.escravo[0].posicao = posicao;
  g_uart.escravo[1].existe = false;    // o segundo driver ainda nao existe
}

static void teste_L01_le_o_encoder() {
  secao("L01  Ler a posicao do encoder pelo driver");
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoder(0x1000, false, 25000);
  rodarComWeb(400);

  const LeituraEncoder L = encoderLer(1);
  nota("bruto %ld, %lu leituras, %lu falhas", (long)L.bruto,
       (unsigned long)L.leituras, (unsigned long)L.falhas);
  nota("UART aberta em %lu bps nos pinos RX=%d TX=%d",
       (unsigned long)g_uart.baudAtual, (int)g_uart.pinRx, (int)g_uart.pinTx);
  checar(L.valido && L.bruto == 25000, "L01a",
         "a contagem do driver chega inteira ao sistema");
  checar(L.leituras > 3 && L.falhas == 0, "L01b",
         "e continua chegando, sem falha");

  // A UART2 tem 16 e 17 como padrao, que aqui sao passo e direcao da
  // junta 1. Se o begin() esquecer os pinos, o braco recebe lixo.
  checar(g_uart.pinRx == PIN_RS485_RX && g_uart.pinTx == PIN_RS485_TX,
         "L01c", "a UART2 nao encosta nos pinos de passo e direcao");

  // Junta 2 sem registrador configurado: nao se pergunta nada a ela.
  const LeituraEncoder L2 = encoderLer(2);
  nota("junta 2: %lu leituras, %lu falhas (registrador nao configurado)",
       (unsigned long)L2.leituras, (unsigned long)L2.falhas);
  checar(!L2.valido && L2.leituras == 0 && L2.falhas == 0, "L01d",
         "junta sem registrador nao vira falha nem numero inventado");
}

static void teste_L02_ordem_das_palavras() {
  secao("L02  Palavra baixa primeiro: o erro que faz a posicao saltar");
  reiniciarSistema();
  prepararRoboCalibrado();

  // Driver manda a palavra BAIXA primeiro, sistema configurado como se
  // fosse a alta. E o defeito classico.
  prepararEncoder(0x1000, false, 0x0001ABCD);
  g_uart.escravo[0].baixaPrimeiro = true;    // o driver, ao contrario
  rodarComWeb(400);
  const int32_t errado = encoderLer(1).bruto;

  encoderPendente = configEncoder;
  encoderPendente.baixaPrimeiro = true;      // agora bate
  enviarComando(CMD_APLICAR_ENCODER);
  rodarComWeb(400);
  const int32_t certo = encoderLer(1).bruto;

  nota("posicao real 0x%08lX", (unsigned long)0x0001ABCD);
  nota("lida com a ordem errada: %ld (0x%08lX)", (long)errado, (unsigned long)errado);
  nota("lida com a ordem certa:  %ld (0x%08lX)", (long)certo,  (unsigned long)certo);
  checar(certo == 0x0001ABCD, "L02a",
         "com a ordem certa o valor bate com o do driver");
  checar(errado != certo, "L02b",
         "com a ordem errada da outro numero -- e por isso a chave existe");
}

static void teste_L03_erro_de_posicao() {
  secao("L03  O grafico do erro: comandado menos medido");
  reiniciarSistema();
  prepararRoboCalibrado();
  // 4000 passos por volta, reducao 10: 111 passos por grau de junta.
  webPost("/api/config?ppv1=4000&red1=10");
  rodarComWeb(120);

  prepararEncoder(0x1000, false, 0);
  rodarComWeb(300);
  encoderZerar(0);
  rodarComWeb(200);

  const LeituraEncoder z = encoderLer(1);
  nota("parado e zerado: medido %.3f, erro %.3f", (double)z.graus, (double)z.erro);
  checar(z.valido && fabsf(z.erro) < 0.01f, "L03a",
         "com o braco parado e o encoder zerado, o erro e zero");

  // O encoder acompanha o comando: uma volta do motor = 36 graus de
  // junta com reducao 10, e 10000 contagens de encoder.
  const long alvo = grausParaPassos(J1, passosParaGraus(J1, posicaoJ1()) + 36.0f);
  moverCoordenado(alvo, posicaoJ2(), 20.0f);
  for (int k = 0; k < 400 && motoresEmMovimento(); k++) {
    g_uart.escravo[0].posicao =
        (int32_t)(passosParaGraus(J1, posicaoJ1()) * 10.0f / 360.0f * 10000.0f);
    rodarComWeb(10);
  }
  rodarComWeb(200);
  const LeituraEncoder ok = encoderLer(1);
  nota("depois de 36 graus: comandado %.2f, medido %.2f, erro %.3f",
       (double)passosParaGraus(J1, posicaoJ1()), (double)ok.graus, (double)ok.erro);
  checar(fabsf(ok.erro) < 0.2f, "L03b",
         "encoder acompanhando o comando: erro fica perto de zero");

  // Agora o motor escorrega: o comando anda, o eixo nao.
  const int32_t travado = g_uart.escravo[0].posicao;
  const long alvo2 = grausParaPassos(J1, passosParaGraus(J1, posicaoJ1()) + 10.0f);
  moverCoordenado(alvo2, posicaoJ2(), 20.0f);
  for (int k = 0; k < 400 && motoresEmMovimento(); k++) {
    g_uart.escravo[0].posicao = travado;      // eixo preso
    rodarComWeb(10);
  }
  rodarComWeb(200);
  const LeituraEncoder perdeu = encoderLer(1);
  nota("com o eixo preso: comandado %.2f, medido %.2f, erro %.2f",
       (double)passosParaGraus(J1, posicaoJ1()), (double)perdeu.graus,
       (double)perdeu.erro);
  checar(perdeu.erro > 9.0f, "L03c",
         "eixo preso enquanto o comando anda: o erro denuncia os graus perdidos");
}

static void teste_L04_driver_mudo_e_excecao() {
  secao("L04  Driver mudo, registrador inexistente e CRC ruim");
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoder(0x1000, false, 777);
  rodarComWeb(300);
  checar(encoderLer(1).valido, "L04a", "comeca lendo bem");

  // Cabo arrancado.
  g_uart.escravo[0].mudo = true;
  rodarComWeb(1600);
  const LeituraEncoder m = encoderLer(1);
  nota("mudo por 1,6 s: valido=%d, idade=%lu ms, falhas=%lu",
       (int)m.valido, (unsigned long)m.idadeMs, (unsigned long)m.falhas);
  checar(!m.valido, "L04b",
         "leitura velha para de valer em vez de virar erro calculado em cima de dado morto");
  checar(m.falhas > 3, "L04c", "e as falhas sao contadas para a tela mostrar");

  // Voltou.
  g_uart.escravo[0].mudo = false;
  rodarComWeb(400);
  checar(encoderLer(1).valido, "L04d", "religado o cabo, volta sozinho");

  // Registrador que nao existe: o driver responde excecao.
  g_uart.escravo[0].excecao = 2;
  rodarComWeb(800);
  nota("com excecao 2: valido=%d", (int)encoderLer(1).valido);
  checar(!encoderLer(1).valido, "L04e",
         "excecao nao vira posicao: registrador errado nao inventa numero");

  // CRC ruim: quadro corrompido nao pode virar leitura.
  g_uart.escravo[0].excecao = 0;
  g_uart.escravo[0].crcRuim = true;
  const uint32_t antes = encoderLer(1).leituras;
  rodarComWeb(800);
  nota("com CRC ruim: leituras %lu -> %lu",
       (unsigned long)antes, (unsigned long)encoderLer(1).leituras);
  checar(encoderLer(1).leituras == antes, "L04f",
         "quadro com CRC ruim e descartado, nao aceito");
}

static void teste_L05_so_leitura_e_so_em_manual() {
  secao("L05  O encoder nunca escreve, e so se configura parado");
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoder(0x1000, false, 100);
  rodarComWeb(300);

  // Nenhuma funcao de escrita Modbus (5, 6, 15, 16) pode sair daqui: um
  // defeito que escrevesse num parametro do servo estragaria a maquina
  // de um jeito que nao se desfaz pela tela.
  nota("o escravo recebeu %lu perguntas, todas de leitura",
       (unsigned long)g_uart.escravo[0].perguntas);
  checar(g_uart.escravo[0].perguntas > 3, "L05a",
         "o barramento esta sendo usado");
  checar(configEncoder.funcao == 3 || configEncoder.funcao == 4, "L05b",
         "so funcoes de leitura sao possiveis na configuracao");

  const int codRuim = webPost("/api/encoder/config?func=6");
  nota("tentativa de configurar funcao 6 (escrever registrador): HTTP %d -- \"%s\"",
       codRuim, webCorpo());
  checar(codRuim == 400, "L05c",
         "funcao de escrita e recusada na porta");

  // Fora do manual nao se reconfigura.
  enviarComando(CMD_GRAVAR_INICIAR);
  rodarComWeb(60);
  const int cod = webPost("/api/encoder/config?reg1=999");
  nota("em modo %d: HTTP %d", (int)modoAtual, cod);
  checar(modoAtual != MODO_MANUAL && cod == 400 && configEncoder.reg[0] == 0x1000,
         "L05d", "com o robo fora do manual a configuracao nao muda");
}

static void teste_L06_a_maquina_do_operador() {
  secao("L06  A configuracao medida na maquina do operador");
  reiniciarSistema();

  // Padrao de fabrica DEPOIS da medicao: funcao 4, registrador 5,
  // 32 bits com a palavra baixa primeiro, encoder de 17 bits.
  nota("padrao: funcao %u, registrador %u, %s, %.0f contagens por volta",
       (unsigned)configEncoder.funcao, (unsigned)configEncoder.reg[0],
       configEncoder.baixaPrimeiro ? "palavra baixa primeiro" : "palavra alta primeiro",
       (double)configEncoder.contagensPorVolta[0]);
  checar(configEncoder.funcao == 3 && configEncoder.reg[0] == 90 &&
         configEncoder.baixaPrimeiro && configEncoder.trintaEDois,
         "L06a", "o padrao sai igual ao que foi medido na maquina");

  // Registrador 0 gravado por versao anterior nao pode virar leitura: 0
  // e o inicio da tabela de PARAMETROS, onde nada muda com o eixo.
  g_nvs.u["encRg1"] = 0;
  g_nvs.u["encRg2"] = 0;
  carregarConfiguracoes();
  nota("com 0 gravado no NVS, o registrador vira %u",
       (unsigned)configEncoder.reg[0]);
  checar(configEncoder.reg[0] == ENC_REG_PADRAO, "L06b",
         "registrador 0 guardado por versao velha e tratado como nao configurado");

  // O caso concreto: os numeros que o driver do operador devolveu.
  reiniciarSistema();
  prepararRoboCalibrado();
  // Os numeros exatos da cacada na maquina do operador: registrador 90
  // valia 61346 com o 91 em 0, e depois de girar o eixo a mao passou a
  // 39440 com o 91 em 1.
  prepararEncoder(90, true, 61346);
  rodarComWeb(400);
  const int32_t lido1 = encoderLer(1).bruto;

  g_uart.escravo[0].posicao = 104976;    // 1 * 65536 + 39440
  rodarComWeb(400);
  const int32_t lido2 = encoderLer(1).bruto;

  nota("antes %ld, depois %ld, variou %ld contagens",
       (long)lido1, (long)lido2, (long)(lido2 - lido1));
  nota("com encoder de 17 bits isso e %.3f volta do motor",
       (double)(lido2 - lido1) / 131072.0);
  checar(lido1 == 61346 && lido2 == 104976, "L06c",
         "os numeros que o driver devolveu na bancada sao remontados iguais");

  // A montagem errada nao pode dar quase certo: tem que dar absurdo, que
  // e como o operador reconhece o engano na tela.
  const int32_t trocado = (int32_t)(((uint32_t)39440 << 16) | 1u);
  nota("montando ao contrario daria %ld -- 1,4 bilhao para tras", (long)trocado);
  checar(trocado != lido2, "L06d",
         "palavra alta primeiro daria um numero que nao e giro nenhum");
}

// ---------------------------------------------------------------------
// O operador ligou um driver so e a tela mostrou "0 leituras, 222
// falhas" nas DUAS juntas, sem nada que dissesse o porque. Contador de
// falha sozinho nao diagnostica: o programa de teste de bancada resolve
// justamente porque mostra os bytes. A tela precisa mostrar o mesmo.
// ---------------------------------------------------------------------
// Pega o valor de um campo de texto do JSON, so para a nota sair legivel.
static const char* jsonTrecho(const char* json, const char* campo) {
  static char buf[120];
  char chave[40];
  snprintf(chave, sizeof(chave), "\"%s\":\"", campo);
  const char* p = strstr(json, chave);
  if (!p) { snprintf(buf, sizeof(buf), "(campo %s ausente)", campo); return buf; }
  p += strlen(chave);
  const char* f = strchr(p, '"');
  size_t n = f ? (size_t)(f - p) : strlen(p);
  if (n >= sizeof(buf)) n = sizeof(buf) - 1;
  memcpy(buf, p, n); buf[n] = '\0';
  return buf;
}

// ---------------------------------------------------------------------
// O caso que explica o "222 falhas": o programa de bancada nunca pediu
// DOIS registradores de uma vez, so um. Se o driver nao aceita a
// pergunta dupla, o teste passa e o sistema falha -- que foi exatamente
// o que aconteceu na maquina do operador.
// ---------------------------------------------------------------------
static void teste_L08_driver_que_so_le_um_registrador() {
  secao("L08  Driver que so responde um registrador por pergunta");
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoder(5, true, 143535);          // 2 * 65536 + 12463
  g_uart.escravo[0].soUmRegistrador = true;

  // Com a pergunta dupla recusada, as primeiras leituras falham.
  rodarComWeb(200);
  nota("logo depois de recusar a pergunta dupla: %lu falhas",
       (unsigned long)encoderLer(1).falhas);

  // E o sistema tem que descobrir sozinho e passar a perguntar um de
  // cada vez, sem ninguem mexer em configuracao nenhuma.
  rodarComWeb(1500);
  const LeituraEncoder L = encoderLer(1);
  nota("depois de cair para a forma provada: bruto %ld, %lu leituras",
       (long)L.bruto, (unsigned long)L.leituras);
  checar(L.valido && L.bruto == 143535, "L08a",
         "o driver que so le um registrador por vez tambem e lido, sozinho");
  checar(L.leituras > 3, "L08b",
         "e continua sendo lido, nao foi sorte de uma vez");

  webGet("/api/encoder");
  nota("quadro: %s", jsonTrecho(webCorpo(), "quadro"));
  checar(strstr(webCorpo(), "1 de cada vez") != nullptr, "L08c",
         "a tela diz de que jeito esta perguntando, sem abrir o codigo");

  // O valor tem que bater com o do driver que aceita os dois de uma vez:
  // e a mesma posicao, so a pergunta muda.
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoder(5, true, 143535);
  rodarComWeb(400);
  nota("mesmo driver aceitando a pergunta dupla: bruto %ld",
       (long)encoderLer(1).bruto);
  checar(encoderLer(1).bruto == 143535, "L08d",
         "as duas formas de perguntar dao o mesmo numero");
}

static void teste_L07_o_quadro_cru_na_tela() {
  secao("L07  Falhou: da para saber o porque sem abrir o codigo?");
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoder(0x1000, false, 4242);
  rodarComWeb(300);

  webGet("/api/encoder");
  const char* corpo = webCorpo();
  nota("lendo bem: %s", jsonTrecho(corpo, "quadro"));
  checar(strstr(corpo, "\"quadro\"") != nullptr, "L07a",
         "a resposta traz o ultimo quadro trocado no fio");
  checar(strstr(corpo, "01 03") != nullptr, "L07b",
         "com os bytes da pergunta, id e funcao a vista");

  // Cabo arrancado: e aqui que o operador precisa da diferenca entre
  // "ninguem respondeu" e "respondeu outra coisa".
  g_uart.escravo[0].mudo = true;
  rodarComWeb(600);
  webGet("/api/encoder");
  nota("mudo: %s", jsonTrecho(webCorpo(), "quadro"));
  checar(strstr(webCorpo(), "silencio") != nullptr, "L07c",
         "driver mudo aparece como silencio, nao so como numero de falha");

  // Junta 2 sem registrador: nao e falha, e ausencia. Chamar de falha
  // manda o operador procurar defeito que nao existe.
  webGet("/api/encoder");
  nota("junta 2 com registrador %u", (unsigned)configEncoder.reg[1]);
  checar(configEncoder.reg[1] == 0 && encoderLer(2).falhas == 0, "L07d",
         "junta nao ligada nao acumula falha para o operador cacar");
}

// ---------------------------------------------------------------------
// A janela entre o ultimo bit e baixar o DE. Num sketch sozinho na placa
// ela e respeitada; aqui dentro ha Wi-Fi, servidor web, cartao e as
// interrupcoes dos motores no mesmo nucleo, e qualquer um deles pode
// estica-la. Quem baixa o DE tem que ser o periferico.
// ---------------------------------------------------------------------
static void teste_L09_de_pelo_hardware() {
  secao("L09  Quem baixa o DE do MAX485");
  reiniciarSistema();
  prepararRoboCalibrado();

  nota("padrao de fabrica: DE por hardware = %d",
       (int)configEncoder.deHardware);
  checar(configEncoder.deHardware, "L09a",
         "de fabrica o DE e baixado pelo periferico, nao pelo firmware");

  prepararEncoder(0x1000, false, 999);
  rodarComWeb(300);
  nota("UART em modo %d (RS485 meio-duplex = %d), RTS no pino %d",
       g_uartIdf.modo, (int)UART_MODE_RS485_HALF_DUPLEX, g_uartIdf.pinoRts);
  checar(g_uartIdf.modo == UART_MODE_RS485_HALF_DUPLEX &&
         g_uartIdf.pinoRts == PIN_RS485_DE, "L09b",
         "a UART entra em RS485 meio-duplex com o DE como RTS");
  checar(encoderLer(1).valido, "L09c",
         "e a leitura continua funcionando desse jeito");

  // Quem tiver fiacao que nao goste do modo por hardware precisa poder
  // voltar sem regravar firmware.
  const int cod = webPost("/api/encoder/config?dehw=0");
  rodarComWeb(300);
  nota("desmarcando na tela: HTTP %d, modo da UART %d", cod, g_uartIdf.modo);
  checar(cod == 200 && !configEncoder.deHardware &&
         g_uartIdf.modo == UART_MODE_UART, "L09d",
         "da para voltar ao controle por GPIO pela tela, sem regravar");
  checar(encoderLer(1).valido || encoderLer(1).leituras > 0, "L09e",
         "e do jeito antigo tambem le");
}

// ---------------------------------------------------------------------
// Atualizar o firmware NAO apaga o NVS. Uma configuracao de encoder
// gravada por uma versao anterior continua valendo e ganha do padrao
// novo -- quem atualizou fica perguntando no registrador errado para
// sempre, e nada na tela diz isso.
// ---------------------------------------------------------------------
static void teste_L10_configuracao_velha_no_nvs() {
  secao("L10  Configuracao de encoder de uma versao anterior");
  reiniciarSistema();

  // O que uma versao anterior deste projeto gravava: funcao 3 na faixa
  // 0x1000, que nesta maquina nao e a posicao.
  g_nvs.u["encFn"]  = 3;
  g_nvs.u["encRg1"] = 0x1000;
  g_nvs.u["encRg2"] = 0x1000;
  carregarConfiguracoes();
  nota("depois de atualizar o firmware: funcao %u, registrador %u",
       (unsigned)configEncoder.funcao, (unsigned)configEncoder.reg[0]);
  checar(configEncoder.funcao == 3 && configEncoder.reg[0] == 0x1000, "L10a",
         "o que estava gravado ganha do padrao novo -- e por isso que da para nao ler nada");

  prepararRoboCalibrado();
  const int cod = webPost("/api/encoder/padroes");
  rodarComWeb(200);
  nota("depois de \"voltar aos padroes medidos\": HTTP %d, funcao %u, "
       "registrador %u, junta 2 em %u",
       cod, (unsigned)configEncoder.funcao, (unsigned)configEncoder.reg[0],
       (unsigned)configEncoder.reg[1]);
  checar(cod == 200 && configEncoder.funcao == ENC_FUNCAO_PADRAO &&
         configEncoder.reg[0] == ENC_REG_PADRAO &&
         configEncoder.baixaPrimeiro && configEncoder.trintaEDois, "L10b",
         "um botao devolve tudo ao que foi medido nesta maquina");
  checar(configEncoder.reg[1] == 0, "L10c",
         "e a junta 2 volta a nascer nao ligada, sem inventar falha");

  // E fica gravado: nao adianta consertar so ate o proximo boot.
  checar(g_nvs.u.count("encRg1") && g_nvs.u["encRg1"] == ENC_REG_PADRAO,
         "L10d", "gravado no NVS, senao voltaria o defeito no proximo boot");

  // So parado.
  enviarComando(CMD_GRAVAR_INICIAR);
  rodarComWeb(60);
  nota("em modo %d: HTTP %d", (int)modoAtual,
       webPost("/api/encoder/padroes"));
  checar(modoAtual != MODO_MANUAL, "L10e",
         "e, como toda configuracao, so em manual");
}

// ---------------------------------------------------------------------
// "Sem resposta" e o fim da linha para quem so tem a tela: nao da para
// saber se o problema e o modulo, o barramento ou o endereco. O programa
// de bancada resolve isso, mas ele roda com o ESP32 SOZINHO na placa --
// e a pergunta que importa e se a linha funciona AQUI DENTRO, com tudo
// mais rodando. Este e o mesmo autoteste, por dentro do sistema.
// ---------------------------------------------------------------------
static void teste_L11_autoteste_dentro_do_sistema() {
  secao("L11  Autoteste da linha RS485 dentro do sistema rodando");
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoder(5, true, 4242);

  // O modulo existe e esta ligado: o eco passa a ser consequencia do que
  // o firmware faz com o RE, nao um botao do banco.
  g_uart.moduloLigado = true;
  g_uart.pinoRe       = PIN_RS485_RE;
  webPost("/api/encoder/testar");
  rodarComWeb(600);

  webGet("/api/encoder/teste");
  const char* rel = webCorpo();
  nota("%s", rel);
  checar(strstr(rel, "MODULO OK") != nullptr, "L11a",
         "o eco prova a ligacao ESP32<->MAX485 aqui dentro, nao so na bancada");
  checar(strstr(rel, "f3 r0") != nullptr && strstr(rel, "f4 r0") != nullptr,
         "L11b",
         "sonda o registrador 0 nas duas funcoes, que e como se acha o driver");
  checar(strstr(rel, "RESPOSTA BOA") != nullptr, "L11c",
         "e mostra o resultado da pergunta de verdade, com os bytes");

  // Depois do teste a leitura normal tem que voltar sozinha: um
  // diagnostico que deixa a maquina pior nao serve.
  rodarComWeb(600);
  nota("depois do teste: %lu leituras", (unsigned long)encoderLer(1).leituras);
  checar(encoderLer(1).valido, "L11d",
         "terminado o teste, a leitura normal volta sozinha");

  // Modulo desligado do ESP32: o eco falha, e o relatorio diz ONDE.
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoder(5, true, 4242);
  g_uart.moduloLigado = false;      // modulo desligado do ESP32
  g_uart.escravo[0].mudo = true;
  webPost("/api/encoder/testar");
  rodarComWeb(900);
  webGet("/api/encoder/teste");
  nota("%s", webCorpo());
  checar(strstr(webCorpo(), "ECO FALHOU") != nullptr, "L11e",
         "sem eco, o relatorio aponta o trecho ESP32<->MAX485");
  checar(strstr(webCorpo(), "SILENCIO") != nullptr, "L11f",
         "e o silencio do driver aparece separado do problema do modulo");
}

// ---------------------------------------------------------------------
// O mapa Modbus do T3D nao esta publicado. O registrador da posicao se
// acha de um jeito so: ler tudo, mover o eixo, e ver o que andou junto.
// Isso estava so no programa de bancada; agora esta na maquina.
// ---------------------------------------------------------------------
static void teste_L12_cacar_o_registrador() {
  secao("L12  Achar o registrador da posicao sem manual nenhum");
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoder(90, true, 61346);      // os numeros da maquina do operador

  webPost("/api/encoder/cacar");
  rodarComWeb(900);
  webGet("/api/encoder/teste");
  nota("%s", webCorpo());
  checar(strstr(webCorpo(), "MOVA O BRACO") != nullptr, "L12a",
         "marcado o estado inicial, o sistema diz o que fazer em seguida");

  // O operador gira o eixo a mao: e exatamente o que o driver do
  // operador mostrou, 61346 -> 104976.
  g_uart.escravo[0].posicao = 104976;
  webPost("/api/encoder/cacar?comparar=1");
  rodarComWeb(900);
  webGet("/api/encoder/teste");
  nota("%s", webCorpo());
  checar(strstr(webCorpo(), "90") != nullptr, "L12b",
         "o registrador que andou junto com o eixo aparece na lista");
  checar(strstr(webCorpo(), "Palpite: registrador 90") != nullptr, "L12c",
         "e o sistema aponta qual dos dois e a palavra baixa, sem o operador deduzir");

  // Eixo parado: dizer "achei" seria pior que dizer "nao achei".
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoder(90, true, 61346);
  webPost("/api/encoder/cacar");
  rodarComWeb(900);
  webPost("/api/encoder/cacar?comparar=1");
  rodarComWeb(900);
  webGet("/api/encoder/teste");
  nota("%s", webCorpo());
  checar(strstr(webCorpo(), "NENHUM registrador mudou") != nullptr, "L12d",
         "sem mover o braco, o sistema diz que nao achou em vez de chutar");

  // Comparar sem marcar nao pode inventar resultado.
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoder(90, true, 61346);
  webPost("/api/encoder/cacar?comparar=1");
  rodarComWeb(600);
  webGet("/api/encoder/teste");
  nota("%s", webCorpo());
  checar(strstr(webCorpo(), "marque o estado inicial") != nullptr, "L12e",
         "comparar sem ter marcado explica o que falta");

  // E a leitura normal volta sozinha depois de tudo.
  rodarComWeb(600);
  nota("depois da cacada: %lu leituras", (unsigned long)encoderLer(1).leituras);
  checar(encoderLer(1).valido, "L12f",
         "terminada a cacada, a leitura normal volta sozinha");
}

// ---------------------------------------------------------------------
// O monitor serial do operador encheu de "/connecttest.txt". E o Windows
// perguntando se a rede tem internet. Respondendo 404 ele conclui que
// nao tem, repete a pergunta sem parar, e chega a largar a rede.
// ---------------------------------------------------------------------
static void teste_J04_teste_de_rede_do_sistema_operacional() {
  secao("J04  Windows perguntando se a rede tem internet");
  reiniciarSistema();

  const int cod = webGet("/connecttest.txt");
  nota("GET /connecttest.txt -> HTTP %d, Location: \"%s\"",
       cod, webCabecalho("Location"));
  checar(cod == 302, "J04a",
         "a sonda do Windows leva redirecionamento, nao 404");
  checar(strstr(webCabecalho("Location"), "192.168.4.1") != nullptr, "J04b",
         "e o destino e o IP da maquina -- o painel abre sozinho na tela");

  // O nome .local dependeria de mDNS, que o Windows so resolve com
  // Bonjour. Mandar para la seria mandar para lugar nenhum.
  checar(strstr(webCabecalho("Location"), ".local") == nullptr, "J04c",
         "nao manda para o nome mDNS, que o Windows nao resolve sozinho");

  // Android e iPhone perguntam de outro jeito, e tem que valer igual.
  const int a = webGet("/generate_204");
  const int i = webGet("/hotspot-detect.html");
  nota("Android /generate_204 -> %d;  iPhone /hotspot-detect.html -> %d", a, i);
  checar(a == 302 && i == 302, "J04d",
         "Android e iPhone tambem, senao so o Windows abre o painel sozinho");

  // Rota que nao e sonda continua sendo 404 com log: esconder tudo seria
  // trocar uma enxurrada por um silencio que engana.
  const int q = webGet("/api/coisa-que-nao-existe");
  nota("rota inventada -> HTTP %d", q);
  checar(q == 404, "J04e",
         "rota de verdade inexistente continua 404, para o defeito aparecer");
}

static void teste_K01_sentido_do_eixo() {
  secao("K01  Trocar o sentido do eixo, inclusive durante a calibracao");
  reiniciarSistema();
  prepararRoboCalibrado();

  const bool antes     = J1.inverterDir;
  const bool sobeAntes = J1.motor->dirSobe;
  nota("junta 1: inverterDir=%d, dirHighCountsUp no gerador=%d",
       (int)antes, (int)sobeAntes);

  checar(webPost("/api/sentido?j=1&v=1") == 200, "K01a",
         "POST /api/sentido inverte a junta em modo manual");
  rodarComWeb(120);
  nota("depois: inverterDir=%d, dirHighCountsUp=%d -- \"%s\"",
       (int)J1.inverterDir, (int)J1.motor->dirSobe, ultimaMensagem);
  checar(J1.inverterDir && J1.motor->dirSobe != sobeAntes, "K01b",
         "o sinal chega no gerador de pulso, nao so na variavel");
  checar(g_nvs.b.count("inv1") && g_nvs.b["inv1"], "K01c",
         "e fica gravado: religar nao volta ao sentido errado");
  checar(fabsf(J2.passosPorGrau) > 0 && !J2.inverterDir, "K01d",
         "a outra junta nao e tocada");

  // Recusas na porta.
  checar(webPost("/api/sentido?j=9&v=1") == 400, "K01e",
         "junta inexistente e recusada");
}

static void teste_K02_sentido_durante_a_calibracao() {
  secao("K02  O sentido errado se descobre calibrando -- da para consertar la?");
  reiniciarSistema();
  prepararRoboCalibrado();

  enviarComando(CMD_CALIB_INICIAR);
  rodarComWeb(200);
  nota("modo=%d etapa=%d (CAL_HOME=%d)", (int)modoAtual, (int)estadoCalib,
       (int)CAL_HOME);
  checar(modoAtual == MODO_CALIBRANDO && estadoCalib == CAL_HOME, "K02a",
         "o assistente para na etapa de referencia");

  // E exatamente aqui que o operador aperta a seta e ve o braco ir para o
  // outro lado. Mandar cancelar o assistente para consertar era pedir
  // para ele desistir.
  const int cod = webPost("/api/sentido?j=1&v=1");
  rodarComWeb(200);
  nota("na etapa de referencia: HTTP %d -- \"%s\"", cod, ultimaMensagem);
  checar(cod == 200 && J1.inverterDir, "K02b",
         "na etapa de referencia da para inverter sem cancelar o assistente");

  // Depois de medir o primeiro limite, NAO: trocar o sinal do eixo agora
  // inverteria o significado do que ja foi medido.
  enviarComando(CMD_CALIB_CONFIRMAR, 0, 0, 0.0f, 0.0f);
  uint32_t t = 0;
  while (estadoCalib != CAL_J1_NEG && t < 5000) { rodarComWeb(20); t += 20; }
  const bool eraInv = J1.inverterDir;
  const int cod2 = webPost("/api/sentido?j=1&v=0");
  rodarComWeb(120);
  nota("na etapa %d: HTTP %d, inverterDir continua %d -- \"%s\"",
       (int)estadoCalib, cod2, (int)J1.inverterDir, ultimaMensagem);
  checar(estadoCalib == CAL_J1_NEG && cod2 == 400 &&
         J1.inverterDir == eraInv, "K02c",
         "depois de medir o primeiro limite o sentido trava, com motivo");
}

static void teste_K03_sentido_com_o_braco_andando() {
  secao("K03  Trocar o sentido com o braco em movimento");
  reiniciarSistema();
  prepararRoboCalibrado();

  // Poe o eixo em movimento de verdade e tenta trocar o sinal por baixo
  // do gerador de pulso: e o jeito mais rapido de mandar o braco para o
  // batente na direcao errada.
  for (int i = 0; i < 5; i++) { enviarComando(CMD_JOG, 1, 1); rodarComWeb(60); }
  const bool andando = motoresEmMovimento();
  const bool eraInv  = J1.inverterDir;
  webPost("/api/sentido?j=1&v=1");
  rodarComWeb(20);
  nota("andando=%d; inverterDir %d -> %d -- \"%s\"", (int)andando,
       (int)eraInv, (int)J1.inverterDir, ultimaMensagem);
  checar(andando && J1.inverterDir == eraInv, "K03a",
         "com o eixo andando o sentido nao muda");
  checar(strstr(ultimaMensagem, "parar") != nullptr, "K03b",
         "e o operador e avisado do porque, em vez de nada acontecer");

  enviarComando(CMD_JOG, 1, 0);
  rodarComWeb(600);
  webPost("/api/sentido?j=1&v=1");
  rodarComWeb(120);
  checar(J1.inverterDir, "K03c", "parado, a mesma troca passa");
}

static void teste_J01_wifi_proprio() {
  secao("J01  A maquina depende de rede de alguem?");
  reiniciarSistema();

  nota("modo do radio: %d (AP=%d, AP_STA=%d)",
       WiFi.modoAtual, WIFI_AP, WIFI_AP_STA);
  checar(WiFi.modoAtual == WIFI_AP, "J01a",
         "o radio sobe so como ponto de acesso, sem modo estacao");
  // Um radio so: em AP+STA o ponto de acesso acompanha o canal do
  // roteador e o tempo de antena e dividido. Era isso que aparecia como
  // atraso no joystick.
  checar(WiFi.staSsid.empty() && !WiFi.tentando, "J01b",
         "nenhuma tentativa de entrar em rede de terceiro");

  nota("ponto de acesso \"%s\", IP fixado: %s",
       WiFi.apSsid.c_str(), WiFi.apIpFixo ? "sim" : "nao");
  checar(WiFi.apSsid == WIFI_AP_SSID && WiFi.apIpFixo, "J01c",
         "o Wi-Fi proprio sobe com IP declarado pelo projeto");
  checar(MDNS.ativo && MDNS.nome == WIFI_NOME_LOCAL, "J01d",
         "robo2dof.local e anunciado");
  nota("painel: http://%s e http://%s.local", redeIpAcesso(), redeNomeLocal());
}

static void teste_J02_endereco_do_painel() {
  secao("J02  O painel sabe dizer por onde se chega nele");
  reiniciarSistema();

  checar(webGet("/api/rede") == 200, "J02a", "GET /api/rede responde");
  const std::string j = webCorpo();
  nota("%s", j.c_str());
  checar(j.find("\"ip\":\"192.168.4.1\"") != std::string::npos, "J02b",
         "o IP do ponto de acesso e o fixo do projeto");
  checar(j.find("\"nome\":\"robo2dof\"") != std::string::npos, "J02c",
         "e o nome mDNS vai junto");

  // As rotas de configuracao de rede sairam: nao pode sobrar meia porta
  // aberta que a pagina nao usa mais.
  checar(webPost("/api/rede/varrer")   == 404 &&
         webPost("/api/rede/conectar") == 404 &&
         webPost("/api/rede/esquecer") == 404 &&
         webGet ("/api/rede/lista")    == 404, "J02d",
         "as rotas do modo estacao nao existem mais");
}

static void teste_J03_qualquer_endereco_cai_no_painel() {
  secao("J03  Entrar na rede da maquina e achar o painel");
  reiniciarSistema();

  // DNS de captura: responde qualquer nome com o IP da maquina. E o que
  // faz o celular oferecer abrir o painel sozinho ao entrar na rede, em
  // vez de reclamar que nao ha internet e pular para os dados moveis.
  nota("DNS na porta %u, dominio \"%s\"", g_dns.porta, g_dns.dominio.c_str());
  checar(g_dns.ligado && g_dns.porta == 53 && g_dns.dominio == "*", "J03a",
         "o DNS de captura responde qualquer nome com o IP da maquina");

  const uint32_t antes = g_dns.pedidos;
  rodarComWeb(50);
  checar(g_dns.pedidos > antes, "J03b",
         "e ele e atendido no laco da tarefa de rede, sem bloquear nada");
}

// =====================================================================
int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);
  printf("\n\033[1mBANCO DE TESTES - RoboCNC v6\033[0m\n");
  printf("firmware real + mocks de Arduino/FastAccelStepper/NVS/FreeRTOS\n");

  if (getenv("VERBOSE")) g_serialSilencioso = false;
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
  teste_A15_fluxo_completo();
  teste_A16_joystick();

  teste_B01_sem_cartao();
  teste_B02_programa_ida_e_volta();
  teste_B03_programa_em_graus();
  teste_B04_arquivo_corrompido();
  teste_B05_nome_de_arquivo();
  teste_B06_trajetoria_binaria();
  teste_B07_config_backup();
  teste_B08_arquivo_durante_execucao();
  teste_B09_registro_de_eventos();

  teste_C01_mensagens_de_recusa();
  teste_C02_braco_fora_da_area();
  teste_C03_cordao_bom_passa();

  teste_E01_aferir_resolucao();
  teste_E02_angulo_da_referencia();
  teste_E03_sem_informar_nada();

  teste_F01_jog_livre_sem_calibracao();
  teste_F02_apagar_calibracao();
  teste_F03_sentido_do_eixo();

  teste_G01_velocidade_igual_entre_juntas();

  teste_H01_velocidade_de_cordao();
  teste_H02_suavidade_da_partida();
  teste_H03_zerar_na_posicao();
  teste_H04_aferir_reducao();
  teste_H05_desenho_vira_programa();
  teste_H06_rotas_da_interface();

  teste_I01_ziguezague_reto();
  teste_I02_ziguezague_na_borda();
  teste_I03_velocidade_entre_trechos();

  teste_L01_le_o_encoder();
  teste_L02_ordem_das_palavras();
  teste_L03_erro_de_posicao();
  teste_L04_driver_mudo_e_excecao();
  teste_L05_so_leitura_e_so_em_manual();
  teste_L06_a_maquina_do_operador();
  teste_L07_o_quadro_cru_na_tela();
  teste_L08_driver_que_so_le_um_registrador();
  teste_L09_de_pelo_hardware();
  teste_L10_configuracao_velha_no_nvs();
  teste_L11_autoteste_dentro_do_sistema();
  teste_L12_cacar_o_registrador();

  teste_K01_sentido_do_eixo();
  teste_K02_sentido_durante_a_calibracao();
  teste_K03_sentido_com_o_braco_andando();

  teste_J01_wifi_proprio();
  teste_J02_endereco_do_painel();
  teste_J03_qualquer_endereco_cai_no_painel();
  teste_J04_teste_de_rede_do_sistema_operacional();



  printf("\n\033[1mRESULTADO: %d passaram, %d anomalias\033[0m\n\n", nPassa, nAnomalia);
  (void)secaoAtual;
  return 0;
}
