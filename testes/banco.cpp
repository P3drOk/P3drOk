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

static void reiniciarSistema() {
  g_nvs = NvsMock();
  g_fs  = FsMock();
  armReiniciarTeste();
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
  // Descarta o transiente do boot.
  registrarContatoOperador();
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



  printf("\n\033[1mRESULTADO: %d passaram, %d anomalias\033[0m\n\n", nPassa, nAnomalia);
  (void)secaoAtual;
  return 0;
}
