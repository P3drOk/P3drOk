// =====================================================================
//  Banco de testes do firmware Robo2dof v6.
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
#include "encoder.h"
#include "correcao.h"
#include "aprender.h"
#include "Preferences.h"
#include "FS.h"
#include "SD.h"
#include <string>
#include <string.h>
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

// O encoder ve o EIXO FISICO, e o eixo so anda quando pulso sai no fio.
// Nao adianta espelhar posicaoJ1(): setCurrentPosition() muda a CONTAGEM
// sem mover o eixo, e e justamente isso que o assentamento faz no fim.
// Espelhar a contagem esconderia o defeito que este cenario procura.
static long g_perdaPassos = 0;     // passos que o eixo perdeu (escorregou)
// Onde o eixo ja estava quando a contagem de pulsos comecou. Depois de
// um boot com encoder absoluto os dois nao coincidem mais: a contagem
// nasce onde o encoder disse, e os pulsos nascem em zero.
static long g_eixoBasePassos = 0;
// So os cenarios de assentamento ligam o espelho. Os outros cravam a
// posicao do escravo a mao, e um espelho sempre ligado atropelaria todos.
static bool g_espelharEixo = false;

// O espelho cobre AS DUAS juntas. Cobria so a primeira, e isso deixava
// qualquer cenario da junta 2 tendo de cravar a posicao do escravo a mao
// -- o que nao e o eixo andando, e sim o teste fingindo. A junta 2 so e
// espelhada quando ela esta no barramento (reg != 0), entao os cenarios
// de um driver so continuam exatamente como estavam.
static long g_perdaPassos2    = 0;
static long g_eixoBasePassos2 = 0;

static void espelharUmEixo(uint8_t k, const Junta& j, long base, long perda) {
  if (!j.motor) return;
  if (k == 2 && configEncoder.reg[1] == 0) return;
  const long fisico = base + (long)j.motor->pulsosGerados - perda;
  const float cv  = configEncoder.contagensPorVolta[k - 1];
  const float red = (j.reducao > 0.001f) ? j.reducao : 1.0f;
  // passos do motor -> voltas do motor -> contagens do encoder.
  const float voltasMotor = (j.passosPorGrau > 0.0f)
      ? ((float)fisico / j.passosPorGrau) * red / 360.0f : 0.0f;
  g_uart.escravo[k - 1].parar();
  g_uart.escravo[k - 1].posicao = encoderLer(k).referencia
                                + (int32_t)lroundf(voltasMotor * cv);
}

static void espelharEixoNoEncoder() {
  if (!g_espelharEixo) return;
  espelharUmEixo(1, J1, g_eixoBasePassos,  g_perdaPassos);
  espelharUmEixo(2, J2, g_eixoBasePassos2, g_perdaPassos2);
}

// Encena perda de passo na junta 2, igual a perderPassos() da junta 1.
static void perderPassos2(float graus) {
  g_perdaPassos2 += lroundf(graus * J2.passosPorGrau);
}

// Encena perda de passo: o eixo fica para tras do que foi comandado.
static void perderPassos(float graus) {
  g_perdaPassos += lroundf(graus * J1.passosPorGrau);
}

// Onde o EIXO parou de verdade, em graus da junta. E este numero que o
// operador ve na peca -- nao a contagem de passos, que e so a conta que o
// firmware faz.
static float eixoFisicoGraus() {
  if (!J1.motor || J1.passosPorGrau <= 0.0f) return 0.0f;
  const long fisico = g_eixoBasePassos + (long)J1.motor->pulsosGerados - g_perdaPassos;
  return (float)fisico / J1.passosPorGrau + J1.grausHome;
}

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
    espelharEixoNoEncoder();
    encoderCicloTeste();
  }
}
// Simula o navegador vivo: heartbeat HTTP a cada 200 ms.
static void rodarComWeb(uint32_t ms) {
  // O heartbeat segue o RELOGIO, nao a contagem de voltas. Um ciclo do
  // banco nao custa 1 ms: leitura de encoder que da tempo esgotado custa
  // 100 ms de relogio simulado, e contar voltas deixaria o vigia de
  // conexao disparar no meio de um cenario que nada tem a ver com isso.
  static uint32_t ultimoHb = 0;
  for (uint32_t i = 0; i < ms; i++) {
    if (!ultimoHb || (uint32_t)(g_millis - ultimoHb) >= 150) {
      registrarContatoOperador();
      ultimoHb = g_millis;
    }
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

// Percorre a calibracao inteira: quatro marcas e nada mais.
//
// A assinatura antiga carregava o angulo declarado na referencia e o
// curso medido com transferidor. Nenhum dos dois existe: a calibracao
// nao pergunta nada. Os parametros ficaram para os cenarios antigos
// continuarem compilando, e sao ignorados.
static bool rodarAssistente(long passosNeg, long passosPos,
                            float = 0.0f, float = 0.0f,
                            float = 0.0f, float = 0.0f) {
  auto ateEtapa = [&](EstadoCalib alvo) {
    uint32_t t = 0;
    while (estadoCalib != alvo && t < 20000) { rodarComWeb(20); t += 20; }
    return estadoCalib == alvo;
  };

  enviarComando(CMD_CALIB_INICIAR);
  if (!ateEtapa(CAL_J1_POS)) return false;

  J1.motor->setCurrentPosition(passosPos);
  enviarComando(CMD_CALIB_CONFIRMAR); ateEtapa(CAL_J1_NEG);
  J1.motor->setCurrentPosition(passosNeg);
  enviarComando(CMD_CALIB_CONFIRMAR); ateEtapa(CAL_J2_POS);
  J2.motor->setCurrentPosition(passosPos);
  enviarComando(CMD_CALIB_CONFIRMAR); ateEtapa(CAL_J2_NEG);
  J2.motor->setCurrentPosition(passosNeg);
  enviarComando(CMD_CALIB_CONFIRMAR);

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
  correcaoReiniciarTeste();
  aprenderReiniciarTeste();
  // O barramento RS485 tambem e estado: um cenario que terminou com o
  // driver mudo deixava o seguinte gastando o tempo esgotado de cada
  // leitura, e o relogio do banco corria mais rapido que o movimento.
  // Cada cenario comeca com os DOIS drivers no barramento -- que e a
  // maquina de verdade, e o que o habilita exige: o SON vai por Modbus
  // desde que deixou de ser fio, e cortar o torque de um driver so
  // deixaria meio braco energizado. Quem quiser encenar a bancada do
  // operador (um driver so) desliga o segundo no proprio cenario, como
  // os testes de encoder fazem.
  g_uart.escravo[0] = EscravoModbus{};
  g_uart.escravo[1] = EscravoModbus{};
  g_uart.escravo[1].id = 2;
  g_uart.escravo[0].velocidade = g_uart.escravo[1].velocidade = 0;
  g_uart.moduloLigado = false;
  g_uart.pinoRe       = -1;
  g_perdaPassos       = 0;
  g_eixoBasePassos    = 0;
  g_perdaPassos2      = 0;
  g_eixoBasePassos2   = 0;
  g_espelharEixo      = false;
  g_millis = 1000;
  g_comandosDescartados = 0;
  setup();
  // Repouso do botao de emergencia: contato NC fechado, o pino ve GND.
  // Vem DEPOIS do setup() de proposito -- e o pinMode(INPUT_PULLUP) de
  // la que poe o pino em HIGH, e HIGH representa NADA LIGADO, que agora
  // (e com razao) e emergencia. Cada cenario comeca com o botao
  // instalado e solto; quem quiser encenar cabo partido poe HIGH.
  g_pinEntrada[PIN_ESTOP] = LOW;
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
  // A maquina de estados do zero absoluto le a configuracao no primeiro
  // ciclo e nao volta atras -- e certo: depois de localizado, nao se
  // relocaliza no meio da sessao. Mas aqui a configuracao chegou DEPOIS
  // do primeiro ciclo, o que no ESP32 nao acontece: la o NVS e lido
  // antes do loop existir. Reiniciar aqui e o que torna este ajudante um
  // BOOT de verdade.
  correcaoReiniciarTeste();
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
  g_pinEntrada[PIN_ESTOP] = ESTOP_NIVEL_ATIVO;
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

  // Solta o botao: contato NC volta a fechar, o pino volta a ver GND.
  g_pinEntrada[PIN_ESTOP] = LOW;
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
// Nomes de campo de um JSON plano, na ordem em que aparecem.
static std::string chavesDoJson(const std::string& js) {
  std::string fora;
  size_t p = 0;
  while ((p = js.find('"', p)) != std::string::npos) {
    const size_t f = js.find('"', p + 1);
    if (f == std::string::npos) break;
    // Chave e o que vem imediatamente antes de ':'.
    if (f + 1 < js.size() && js[f + 1] == ':') {
      fora += js.substr(p + 1, f - p - 1);
      fora += ' ';
      // Pula o valor, para nao confundir texto de mensagem com chave.
      size_t v = f + 2;
      if (v < js.size() && js[v] == '"') {
        v = js.find('"', v + 1);
        if (v == std::string::npos) break;
      }
      p = v + 1;
      continue;
    }
    p = f + 1;
  }
  return fora;
}

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
    "REPRODUZINDO", "J1_VOLTA_NEG", 2u,
    -2000000L, -2000000L, -359.99f, -359.99f, -1999.9f, -1999.9f,
    "false","false","false","false","false","false","false","false",
    -359.9f, 359.9f, -359.9f, 359.9f,
    1500u, 4294967295UL, 100u, 200u,
    40u, 40u, 100u, "false", 999.9f,
    999.9f, "false","false","false",
    180000UL, 180000UL, 180000UL, 999999UL, 999999UL,
    999999UL, 999.999f, 999999UL, 999.999f,
    "false","false", 255u,
    40u,
    180000.f, 180000.f, 9999.9f, 9999.99f, 9999.99f,
    9999.9f, 9999.9f, 90.0f, -9999.9f, 9999.9f,
    "false","false","false", 255u,
    "false","false", 4294967295UL, 4294967295UL,
    -999.99f, -999.99f, "false", "false", 100u,
    "false", -9999.0f, 9999.0f, -9999.0f, 9999.0f,
    65535u, 65535u, 65535u, "false", 255u,
    "false", "false",
    9.99, 9.99, 99999.999, 99999.999,
    msg);

  checar(n < 1520, "A10a", "o JSON de status precisa caber no buffer de 1520 bytes");
  nota("pior caso medido: %d bytes. Buffer declarado em servidor_web.cpp: 1520.", n);
  if (n >= 1520) {
    nota("snprintf trunca sem erro: a resposta sai como JSON invalido, o");
    nota("r.json() do navegador lanca excecao, o contador 'quedas' sobe e a");
    nota("interface anuncia 'sem comunicacao' com o robo funcionando.");
  }

  // A copia acima e o pior caso; ela so vale enquanto tiver os MESMOS
  // campos do handler de verdade. Ja ficou para tras uma vez -- cinco
  // campos novos no firmware e nenhum aqui -- e um guarda que mede um
  // formato velho mede folga que nao existe.
  reiniciarSistema();
  webGet("/api/status");
  const std::string vivo = webCorpo();
  const std::string chavesVivas = chavesDoJson(vivo);
  const std::string chavesCopia = chavesDoJson(json);
  if (chavesVivas != chavesCopia) {
    nota("vivo : %s", chavesVivas.c_str());
    nota("copia: %s", chavesCopia.c_str());
  }
  checar(chavesVivas == chavesCopia, "A10b",
         "a copia do pior caso tem os mesmos campos do /api/status de verdade");
  checar(!vivo.empty() && vivo[vivo.size() - 1] == '}', "A10c",
         "e a resposta viva fecha em '}' -- JSON truncado quebra a interface inteira");
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
      "ROBO2DOF-PROG 1\nelos=200.000,200.000\npontos=2\n"
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
      "ROBO2DOF-CFG 1\nvelN=9999999\nl1=200\n";
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
static void teste_E01_resolucao_declarada() {
  secao("E01  Resolucao digitada errada: o que a maquina reporta?");
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
  rodarAssistente(-meio, +meio);
  const float cursoCru = J1.grausMax - J1.grausMin;

  checar(fabsf(cursoCru - 200.0f) < 1.0f, "E01a",
         "o curso reportado sai da resolucao declarada: e ela que traduz "
         "pulso em grau");
  nota("%.2f pulsos/grau digitados -> curso calculado de %.1f graus",
       ppgAntes, cursoCru);
  nota("O braco de verdade girou 100. A conta esta certa; o numero que");
  nota("entrou nela e que estava errado -- e o redutor e onde se conserta.");

  // O conserto e declarar o redutor. E o unico numero que a calibracao
  // nao mede: com um sensor so, antes do redutor, nenhuma medida revela
  // a relacao dele.
  prepararConfigPendente();
  configPendente.red1 = 2.0f;
  configPendente.red2 = 2.0f;
  enviarComando(CMD_APLICAR_CONFIG); rodarComWeb(60);

  const float cursoCerto = J1.grausMax - J1.grausMin;
  nota("redutor declarado 2:1 -> %.2f pulsos/grau, curso %.1f graus",
       J1.passosPorGrau, cursoCerto);
  checar(fabsf(cursoCerto - 100.0f) < 1.0f, "E01b",
         "declarado o redutor, o curso medido passa a sair nos graus reais "
         "-- sem refazer a calibracao");

  // E o braco parado no meio do curso le zero: o zero E o meio.
  J1.motor->setCurrentPosition(0); rodarComWeb(5);
  checar(fabsf(passosParaGraus(J1, posicaoJ1())) < 0.01f, "E01c",
         "no meio do curso o software le zero: o zero e o meio do curso");
  nota("pulso 0 -> %.3f graus", passosParaGraus(J1, posicaoJ1()));
}

// ---------------------------------------------------------------------
// E02: o ZERO sai da propria medida.
//
// Antes o operador declarava, num campo, o angulo real do braco na
// posicao de referencia. Era o unico jeito de a cinematica saber para
// onde o braco aponta -- e era mais um numero para errar, com o sintoma
// de o desenho na tela sair girado em relacao a maquina.
//
// Agora o zero e o MEIO DO CURSO. Nao se pergunta nada, e a area util
// nasce centrada: os limites saem -curso/2 e +curso/2, e nenhuma postura
// comeca encostada num batente.
// ---------------------------------------------------------------------
static void teste_E02_o_zero_e_o_meio_do_curso() {
  secao("E02  O zero sai da medida: e o meio do curso");
  reiniciarSistema();
  enviarComando(CMD_SERVOS, 1); rodarComWeb(30);

  // Marcas assimetricas de proposito: o operador nao para no meio, ele
  // para nos batentes, e eles raramente sao simetricos em relacao a onde
  // a contagem estava.
  const float ppg = J1.passosPorGrau;
  const long pos = (long)(+100.0f * ppg);
  const long neg = (long)( -20.0f * ppg);
  rodarAssistente(neg, pos);

  nota("marcas em %ld e %ld pulsos -> curso J1 de %.1f a %.1f graus",
       neg, pos, J1.grausMin, J1.grausMax);
  checar(fabsf(J1.grausMin + 60.0f) < 0.6f &&
         fabsf(J1.grausMax - 60.0f) < 0.6f, "E02a",
         "o curso de 120 graus sai centrado no zero, sem ninguem declarar "
         "angulo nenhum");

  // O braco esta parado no limite negativo, que foi a ultima marca. Ele
  // tem de ler o limite negativo, nao zero.
  const float ondeEsta = passosParaGraus(J1, posicaoJ1());
  nota("o braco ficou no limite negativo e le %.2f graus", ondeEsta);
  checar(fabsf(ondeEsta - J1.grausMin) < 0.6f, "E02b",
         "e a contagem continua descrevendo onde o braco parou: o "
         "deslocamento do zero move a regua, nao o braco");

  // Ida e volta exata: sem isso todo ponto gravado escorregaria.
  const long p = grausParaPassos(J1, 47.5f);
  checar(fabsf(passosParaGraus(J1, p) - 47.5f) < 0.02f, "E02c",
         "graus -> passos -> graus fecha");
  nota("47.5 graus -> %ld passos -> %.3f graus", p, passosParaGraus(J1, p));
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
  // Sem limites o jog continua livre -- e o PROGRAMA tambem roda.
  //
  // Ele era recusado por falta de calibracao. Os pontos, porem, foram
  // gravados nesta mesma regua: executa-los devolve o braco aos mesmos
  // lugares. O que se perde sem calibracao e a protecao de curso.
  progLimpar();
  for (int i = 0; i < 2; i++) {
    J1.motor->setCurrentPosition(grausParaPassos(J1, 10.0f + 15.0f * i));
    rodarComWeb(5);
    const char* mp = nullptr;
    progAdicionarPonto(posicaoJ1(), posicaoJ2(), &mp);
  }
  const char* m = nullptr;
  const bool progRodou = progIniciar(true, &m);
  nota("sem limites medidos: jog andou=%d, programa iniciou=%d (%s)",
       (int)(posicaoJ1() != antes), (int)progRodou, m ? m : "sem motivo");
  checar(posicaoJ1() != antes && progRodou,
         "F02d", "sem calibracao a maquina opera igual: jog livre E programa "
                 "rodando -- o que falta e a protecao de curso, nao a operacao");
  progParar();
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


// ---------------------------------------------------------------------
// "Restaurar padroes" e "apagar tudo" nao sao o mesmo botao, e a
// diferenca entre eles e a instalacao inteira: horas de calibracao, a
// mesa ensinada, o zero absoluto. Um operador que apertasse o errado
// perderia isso sem aviso -- por isso os dois estao aqui, lado a lado, e
// o banco exige que um NAO faca o que o outro faz.
// ---------------------------------------------------------------------
static void teste_F04_restaurar_padroes_e_apagar_tudo() {
  secao("F04  Restaurar padroes contra apagar tudo");
  reiniciarSistema();
  prepararRoboCalibrado();
  rodarComWeb(60);

  // Uma instalacao de verdade: calibracao, mesa ensinada, zero absoluto
  // e velocidade fora do padrao.
  mesaEnsinarCanto(200.0f, 300.0f);
  mesaEnsinarCanto(500.0f, 600.0f);
  areaMesa.definida = true;
  configZero.ensinado[0] = true;
  velNormal = 12.5f;
  salvarConfiguracoes();
  nota("instalado: mesa=%d, zero ensinado=%d, calibrada=%d, velNormal=%.1f",
       (int)areaMesa.definida, (int)configZero.ensinado[0],
       (int)J1.calibrada, (double)velNormal);

  // ---- restaurar padroes: mexe em PARAMETRO e mais nada --------------
  webPost("/api/config/reset");
  rodarComWeb(120);
  nota("apos restaurar padroes: velNormal=%.1f, mesa=%d, zero=%d, calibrada=%d",
       (double)velNormal, (int)areaMesa.definida,
       (int)configZero.ensinado[0], (int)J1.calibrada);
  checar(velNormal == VEL_NORMAL_PADRAO, "F04a",
         "restaurar padroes devolve os parametros de fabrica");
  checar(areaMesa.definida && configZero.ensinado[0] && J1.calibrada, "F04b",
         "e NAO leva junto a mesa, o zero e a calibracao -- que sao a instalacao");

  // ---- apagar tudo: as travas ----------------------------------------
  const uint32_t reinicios = g_espReinicios;
  int cod = webPost("/api/apagar/tudo");
  nota("sem a palavra: HTTP %d -- \"%s\"", cod, webCorpo());
  checar(cod == 400 && g_espReinicios == reinicios, "F04c",
         "apagar tudo sem digitar a palavra nao apaga nada");

  cod = webPost("/api/apagar/tudo?conf=apagar%20tudo");
  nota("palavra errada: HTTP %d -- \"%s\"", cod, webCorpo());
  checar(cod == 400 && g_espReinicios == reinicios, "F04d",
         "e palavra parecida tambem nao serve: e a palavra exata ou nada");

  enviarComando(CMD_GRAVAR_INICIAR);
  rodarComWeb(60);
  cod = webPost("/api/apagar/tudo?conf=APAGAR");
  nota("em modo %d: HTTP %d -- \"%s\"", (int)modoAtual, cod, webCorpo());
  checar(modoAtual != MODO_MANUAL && cod == 400 && g_espReinicios == reinicios,
         "F04e", "fora do modo manual nao se apaga a maquina");
  enviarComando(CMD_GRAVAR_PARAR);
  rodarComWeb(60);

  // ---- apagar tudo de verdade ----------------------------------------
  // O eixo fica energizado antes: reiniciar a placa com torque deixa o
  // driver sozinho por um segundo, e quem faz isso e o comando, nao o
  // operador.
  enviarComando(CMD_SERVOS, 1);
  rodarComWeb(60);
  cod = webPost("/api/apagar/tudo?conf=APAGAR");
  rodarComWeb(120);
  nota("HTTP %d, reinicios=%lu (antes %lu), servos=%d, chaves no NVS=%u",
       cod, (unsigned long)g_espReinicios, (unsigned long)reinicios,
       (int)servosLigados,
       (unsigned)(g_nvs.u.size() + g_nvs.l.size() + g_nvs.f.size() +
                  g_nvs.b.size() + g_nvs.s.size()));
  checar(cod == 200 && g_espReinicios == reinicios + 1, "F04f",
         "com a palavra certa e o robo parado, a maquina apaga e reinicia");
  checar(!servosLigados, "F04g",
         "e desliga o torque antes de reiniciar: nao deixa o driver sozinho");
  checar(g_nvs.u.empty() && g_nvs.l.empty() && g_nvs.f.empty() &&
         g_nvs.b.empty() && g_nvs.s.empty(), "F04h",
         "o NVS fica VAZIO -- inclusive chave deixada por firmware anterior");

  // O que importa e o que a maquina vira DEPOIS do reinicio: sem
  // calibracao, sem mesa, sem zero. Aqui o banco encena o boot.
  setup();
  rodarComWeb(60);
  nota("apos o reinicio: calibrada=%d, mesa=%d, zero=%d, velNormal=%.1f",
       (int)J1.calibrada, (int)areaMesa.definida,
       (int)configZero.ensinado[0], (double)velNormal);
  checar(!J1.calibrada && !areaMesa.definida && !configZero.ensinado[0] &&
         velNormal == VEL_NORMAL_PADRAO, "F04i",
         "a maquina volta recem-montada: sem calibracao, sem mesa e sem zero");
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
  // O ESCRAVO PRIMEIRO. Aplicar a configuracao com o driver ainda
  // respondendo do endereco antigo faz o sistema ler valor de outro
  // registrador por um instante -- e a diferenca entre esse valor e o
  // primeiro valor de verdade vira um salto de dezenas de milhoes de
  // contagens em "passos acumulados". Nao e defeito do firmware: e o
  // ajudante do banco encenando uma troca que nao existe na maquina.
  g_uart.escravo[0] = EscravoModbus{};
  g_uart.escravo[0].id = 1;
  g_uart.escravo[0].funcao = 3;
  g_uart.escravo[0].regBase = reg;
  g_uart.escravo[0].baixaPrimeiro = baixaPrimeiro;
  g_uart.escravo[0].posicao = posicao;
  // O driver 2 continua NO BARRAMENTO -- so a POSICAO da junta 2 e que
  // nao esta configurada (reg[1] = 0, abaixo). Sao coisas diferentes, e
  // a diferenca passou a importar quando o habilita virou Modbus: o SON
  // vai para os dois drivers, e um driver ausente recusaria habilitar a
  // maquina inteira num cenario que so queria testar leitura de um eixo.
  g_uart.escravo[1] = EscravoModbus{};
  g_uart.escravo[1].id = 2;
  g_uart.escravo[1].velocidade = 0;

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
  // O curso em PASSOS nao muda com a resolucao declarada, entao mudar a
  // resolucao encolheu a faixa em GRAUS para +/-22,5. Este cenario move o
  // eixo 46 graus de proposito, e uma maquina que nao alcanca 46 graus
  // nunca chegaria la -- nem o encoder nem o vigia julgariam nada. Refaz
  // o curso para +/-90 na resolucao nova, que e o que uma maquina de
  // verdade com essa engrenagem teria.
  {
    const long p = (long)(90.0f * J1.passosPorGrau);
    J1.passosMin = -p; J1.passosMax = p;
    J2.passosMin = -p; J2.passosMax = p;
    recalcularResolucao();
    rodarComWeb(20);
  }

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
  checar(perdeu.erro > 1.0f, "L03c",
         "eixo preso enquanto o comando anda: o erro denuncia os graus perdidos");

  // E, desde que existe vigilancia de travamento, o erro nao cresce ate
  // o fim do movimento: o sistema PARA o eixo. Continuar dando pulso
  // contra o batente aquece o servo e torce a mecanica -- denunciar sem
  // parar seria contar o acidente em vez de evitar.
  const Travamento t = correcaoTravamento();
  nota("travamento: ativo=%d, junta %u, total %lu",
       (int)t.ativo, (unsigned)t.junta, (unsigned long)t.total);
  checar(t.ativo && t.junta == 1, "L03d",
         "e o sistema acusa o travamento, em vez de so mostrar o erro crescendo");
  checar(!motoresEmMovimento(), "L03e",
         "e para o eixo: nao fica forcando contra o batente");
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
// Pega um campo numerico do JSON, so para a nota sair legivel.
static const char* jsonTrecho2(const char* json, const char* campo) {
  static char buf[80];
  char chave[40];
  snprintf(chave, sizeof(chave), "\"%s\":", campo);
  const char* p = strstr(json, chave);
  if (!p) { snprintf(buf, sizeof(buf), "(sem %s)", campo); return buf; }
  p += strlen(chave);
  size_t n = 0;
  while (n < sizeof(buf) - 1 && p[n] && p[n] != ',' && p[n] != '}') { buf[n] = p[n]; n++; }
  buf[n] = '\0';
  static char saida[100];
  snprintf(saida, sizeof(saida), "%s = %s", campo, buf);
  return saida;
}

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
static void teste_L08_uma_pergunta_dois_registradores() {
  secao("L08  Uma pergunta so, dois registradores");
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoder(90, true, 143535);         // 2 * 65536 + 12463
  rodarComWeb(300);

  const uint32_t perguntas = g_uart.escravo[0].perguntas;
  const uint32_t leituras  = encoderLer(1).leituras;
  nota("%lu leituras em %lu perguntas",
       (unsigned long)leituras, (unsigned long)perguntas);
  checar(leituras > 3 && encoderLer(1).bruto == 143535, "L08a",
         "os 32 bits vem de UMA pergunta, como o monitor do operador faz");
  // Uma pergunta por leitura, nao duas ou tres. A leitura de dois
  // registradores de uma vez tambem e ATOMICA: o par sai do mesmo
  // instante do contador, sem risco de a palavra baixa dar a volta entre
  // duas perguntas.
  checar(perguntas <= leituras + 2, "L08b",
         "uma pergunta por leitura, e o par sai do mesmo instante");

  webGet("/api/encoder");
  nota("quadro: %s", jsonTrecho(webCorpo(), "quadro"));
  // 00 5A 00 02 = registrador 90, DOIS registradores.
  checar(strstr(webCorpo(), "00 5A 00 02") != nullptr, "L08c",
         "e o quadro na tela mostra os dois registradores sendo pedidos");

  // Driver que recusa a pergunta dupla existe. O sistema nao tem um jeito
  // secreto de contornar isso: ele DIZ que a pergunta foi recusada, que e
  // o que manda o operador olhar a configuracao em vez de esperar.
  g_uart.escravo[0].soUmRegistrador = true;
  rodarComWeb(600);
  const LeituraEncoder R = encoderLer(1);
  nota("driver recusando a pergunta dupla: valido=%d, motivo=%u",
       (int)R.valido, (unsigned)R.motivo);
  checar(!R.valido && R.motivo == MOTIVO_EXCECAO, "L08d",
         "driver que recusa a pergunta e reportado, nao contornado em silencio");
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
static void teste_L09_o_de_sobe_de_verdade() {
  secao("L09  O DE do MAX485 sobe quando falamos, e desce depois");
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoder(90, true, 999);
  rodarComWeb(400);

  // Se o DE nunca subir, o quadro nao chega a sair no barramento e o
  // driver nao tem o que responder: silencio absoluto, para sempre. Foi
  // exatamente isso na maquina do operador enquanto o periferico da UART
  // cuidava do DE em vez do firmware -- e nenhum cenario pegava, porque
  // nenhum olhava o pino.
  nota("DE (pino %d): %lu subidas, agora em %d;  RE em %d",
       (int)PIN_RS485_DE, (unsigned long)g_subidas[PIN_RS485_DE],
       g_pinSaida[PIN_RS485_DE], g_pinSaida[PIN_RS485_RE]);
  checar(g_subidas[PIN_RS485_DE] > 3, "L09a",
         "o DE sobe a cada pergunta -- sem isso nada sai no barramento");
  checar(g_pinSaida[PIN_RS485_DE] == LOW, "L09b",
         "e volta a descer, senao a linha fica presa por nos");
  checar(g_pinSaida[PIN_RS485_RE] == LOW, "L09c",
         "e o RE fica em baixo, escutando, quando nao estamos falando");
  checar(encoderLer(1).valido, "L09d", "e a leitura funciona desse jeito");
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

  // Primeiro giro.
  g_uart.escravo[0].posicao = 90000;
  webPost("/api/encoder/cacar?comparar=1");
  rodarComWeb(900);
  webGet("/api/encoder/teste");
  nota("%s", webCorpo());
  checar(strstr(webCorpo(), "MESMO sentido") != nullptr, "L12b",
         "um giro so nao conclui: o sistema pede o segundo, e diz por que");

  // Segundo giro, mesmo sentido. 61346 -> 90000 -> 104976.
  g_uart.escravo[0].posicao = 104976;
  webPost("/api/encoder/cacar?comparar=1");
  rodarComWeb(900);
  webGet("/api/encoder/teste");
  nota("%s", webCorpo());
  checar(strstr(webCorpo(), "O PAR E 90 (baixa) e 91 (alta)") != nullptr, "L12c",
         "com os dois giros no mesmo sentido, o par da posicao e provado");

  // ---------------------------------------------------------------
  // O caso que a versao anterior errava na maquina do operador: um
  // registrador que vai de 0 para 65535 -- o maior salto da lista, mas
  // que com sinal e -1 -- e o vizinho pulando para os dois lados. Isso
  // e erro de seguimento, e nao pode ser apontado como posicao.
  // ---------------------------------------------------------------
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoder(90, true, 61346);
  webPost("/api/encoder/cacar");
  rodarComWeb(900);

  // A posicao NAO anda; o vizinho ruidoso oscila entre os dois giros.
  g_uart.escravo[0].ruidoReg = 94;
  g_uart.escravo[0].ruidoValor = 65535;      // -1
  webPost("/api/encoder/cacar?comparar=1");
  rodarComWeb(900);
  g_uart.escravo[0].ruidoValor = 30;         // voltou para o outro lado
  webPost("/api/encoder/cacar?comparar=1");
  rodarComWeb(900);
  webGet("/api/encoder/teste");
  nota("%s", webCorpo());
  checar(strstr(webCorpo(), "O PAR E") == nullptr, "L12d",
         "registrador que oscila nao e apontado como posicao, por maior que seja o salto");
  checar(strstr(webCorpo(), "MESMO LADO") != nullptr, "L12e",
         "e o relatorio explica o crivo, em vez de so dizer que nao achou");

  // Comparar sem marcar nao pode inventar resultado.
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoder(90, true, 61346);
  webPost("/api/encoder/cacar?comparar=1");
  rodarComWeb(600);
  webGet("/api/encoder/teste");
  nota("%s", webCorpo());
  checar(strstr(webCorpo(), "marque o estado inicial") != nullptr, "L12f",
         "comparar sem ter marcado explica o que falta");

  // E a leitura normal volta sozinha depois de tudo.
  rodarComWeb(600);
  nota("depois da cacada: %lu leituras", (unsigned long)encoderLer(1).leituras);
  checar(encoderLer(1).valido, "L12g",
         "terminada a cacada, a leitura normal volta sozinha");
}

// ---------------------------------------------------------------------
// Velocidade, sentido, RPM e passos acumulados. O operador escreveu um
// monitor proprio que calcula tudo isso e pediu para trazer para o
// sistema. Aqui o calculo fica no FIRMWARE e nao no navegador: a tarefa
// le a 20 Hz e o painel consulta a 4 Hz -- medir no navegador seria usar
// uma regua cinco vezes mais grossa que a disponivel.
// ---------------------------------------------------------------------
static void teste_L13_velocidade_sentido_e_passos() {
  secao("L13  Velocidade, sentido, RPM e passos acumulados");
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoder(90, true, 100000);
  rodarComWeb(300);

  // Eixo parado: nao pode inventar velocidade nem sentido.
  const LeituraEncoder P = encoderLer(1);
  nota("parado: velocidade %.1f c/s, sentido %d", (double)P.velocidade, (int)P.sentido);
  checar(P.sentido == 0 && fabsf(P.velocidade) < 1.0f, "L13a",
         "com o eixo parado nao ha sentido nem velocidade");

  // Anda para um lado. O eixo tem de estar andando NA HORA da amostra:
  // mover e depois deixar parado 100 ms faz a ultima leitura ter delta
  // zero -- que e a resposta certa para um eixo que parou.
  g_uart.escravo[0].girar(20000);     // 20000 contagens por segundo
  rodarComWeb(800);
  const LeituraEncoder A = encoderLer(1);
  nota("subindo: bruto %ld, leituras %lu, delta %+ld, velocidade %.0f c/s, %.2f rpm, sentido %d",
       (long)A.bruto, (unsigned long)A.leituras,
       (long)A.delta, (double)A.velocidade, (double)A.rpm, (int)A.sentido);
  checar(A.sentido == 1 && A.velocidade > 0, "L13b",
         "andando para um lado, o sentido e a velocidade acompanham");
  checar(A.rpm > 0 && A.rpm < A.velocidade, "L13c",
         "o RPM sai da velocidade dividida pelas contagens por volta");

  // Volta para o outro lado: tem de virar o sentido e contar UMA inversao.
  const uint32_t invAntes = A.inversoes;
  g_uart.escravo[0].girar(-20000);    // volta no mesmo passo
  rodarComWeb(800);
  g_uart.escravo[0].parar();
  const LeituraEncoder B = encoderLer(1);
  nota("descendo: velocidade %.0f c/s, sentido %d, inversoes %lu -> %lu",
       (double)B.velocidade, (int)B.sentido,
       (unsigned long)invAntes, (unsigned long)B.inversoes);
  checar(B.sentido == -1 && B.velocidade < 0, "L13d",
         "voltando, o sentido inverte e a velocidade fica negativa");
  checar(B.inversoes == invAntes + 1, "L13e",
         "e conta UMA inversao, nao uma por leitura");

  // Passos acumulados: somam o caminho andado, nao a diferenca entre
  // pontas. Foi e voltou 16000 contagens: o total e 32000, nao zero.
  nota("posicao voltou para %ld (comecou em 100000); passos acumulados %lu",
       (long)B.bruto, (unsigned long)B.passosTotais);
  // Foi ate a ponta e voltou: o caminho andado e DOIS cursos, e a
  // diferenca entre as pontas e perto de zero. O numero esperado sai da
  // faixa que o proprio encoder registrou, nao de um valor cravado --
  // assim o teste continua valendo se o relogio do banco mudar de passo.
  const uint32_t curso = (uint32_t)(B.brutoMax - B.brutoMin);
  const uint32_t esperado = curso * 2;
  nota("curso medido %lu, ida e volta esperada %lu",
       (unsigned long)curso, (unsigned long)esperado);
  checar(curso > 10000 &&
         B.passosTotais > esperado * 9 / 10 &&
         B.passosTotais < esperado * 11 / 10, "L13f",
         "passos acumulados somam o caminho andado, nao a diferenca entre pontas");
  const int32_t voltouPara = B.bruto - 100000;
  checar(voltouPara > -2000 && voltouPara < 2000, "L13g",
         "e a posicao voltou para perto de onde comecou, provando que era ida e volta");

  // Tremor de um passo com o eixo parado nao pode virar inversao: e o
  // que faria o contador de inversoes -- que serve para achar folga --
  // nao valer nada.
  const uint32_t invEstavel = encoderLer(1).inversoes;
  g_uart.escravo[0].parar();
  const int32_t base = g_uart.escravo[0].posicao;
  for (int k = 0; k < 24; k++) {
    g_uart.escravo[0].posicao = base + (k % 2);
    rodarComWeb(30);
  }
  nota("tremendo um passo por 12 leituras: inversoes %lu -> %lu",
       (unsigned long)invEstavel, (unsigned long)encoderLer(1).inversoes);
  checar(encoderLer(1).inversoes == invEstavel && encoderLer(1).sentido == 0,
         "L13h", "tremor de um passo e parado, nao inversao");

  // Cabo caindo nao pode deixar a tela dizendo que o eixo continua indo.
  g_uart.escravo[0].mudo = true;
  rodarComWeb(400);
  nota("cabo caido: velocidade %.1f, sentido %d",
       (double)encoderLer(1).velocidade, (int)encoderLer(1).sentido);
  checar(fabsf(encoderLer(1).velocidade) < 0.01f && encoderLer(1).sentido == 0,
         "L13i", "sem leitura a velocidade zera, em vez de congelar a ultima");

  // E tudo isso chega na tela.
  g_uart.escravo[0].mudo = false;
  rodarComWeb(300);
  webGet("/api/encoder");
  nota("%s", jsonTrecho2(webCorpo(), "vel"));
  checar(strstr(webCorpo(), "\"vel\"") && strstr(webCorpo(), "\"rpm\"") &&
         strstr(webCorpo(), "\"sent\"") && strstr(webCorpo(), "\"passos\"") &&
         strstr(webCorpo(), "\"inv\""), "L13j",
         "velocidade, rpm, sentido, passos e inversoes chegam ao painel");
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

// Manda o braco para um angulo, encena a perda no meio do caminho, e
// espera o movimento (e o assentamento) terminarem.
static int irComPerda(float t1, float t2, float perdaGraus) {
  const int cod = webPost((std::string("/api/mover?t1=") + std::to_string((int)t1) +
           "&t2=" + std::to_string((int)t2)).c_str());
  rodarComWeb(60);
  if (perdaGraus != 0.0f) perderPassos(perdaGraus);
  uint32_t t = 0;
  while (modoAtual != MODO_MANUAL && t < 8000) { rodarComWeb(20); t += 20; }
  return cod;
}

// =====================================================================
//  M - Assentamento pelo encoder (correcao de posicao)
// =====================================================================
// O incomodo do operador: "saio de uma posicao e volto, e ela nao e mais
// a mesma". Sem assentamento o erro de um movimento entra no proximo e o
// desvio cresce sem nunca voltar.
//
// Metade destes cenarios prova que a correcao FUNCIONA. A outra metade
// prova que ela NAO MEXE quando nao deve -- que e a parte que importa
// numa maquina que solda.
// ---------------------------------------------------------------------

static void teste_M01_assentar_no_fim_do_movimento() {
  secao("M01  Chegou: o encoder confere e retoca");
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoder(90, true, 0);
  rodarComWeb(300);
  enviarComando(CMD_ENCODER_ZERAR, 0);
  rodarComWeb(120);
  g_espelharEixo = true;      // dagora o encoder segue o eixo de verdade

  nota("tolerancia %.2f grau, teto %.2f grau, %u tentativas",
       (double)configCorrecao.toleranciaGraus,
       (double)configCorrecao.maxCorrecaoGraus,
       (unsigned)configCorrecao.tentativas);
  checar(configCorrecao.ativa, "M01a", "de fabrica o assentamento vem ligado");

  // Vai para 20 graus e perde meio grau no caminho -- perda de passo
  // tipica. O que importa nao e a contagem: e onde o EIXO parou.
  irComPerda(20, 10, 0.5f);
  const ResumoCorrecao rc = correcaoResumo();
  nota("faltavam %+.2f grau ao chegar; %u retoque(s); \"%s\"",
       (double)rc.erroInicial1, (unsigned)rc.tentativas, rc.motivo);
  nota("eixo parou em %.3f grau (alvo 20)", (double)eixoFisicoGraus());
  checar(rc.tentativas > 0, "M01b",
         "meio grau de perda faz o sistema retocar, em vez de deixar passar");
  checar(fabsf(eixoFisicoGraus() - 20.0f) < 0.15f, "M01c",
         "e o EIXO acaba no alvo, nao meio grau atras dele");

  // A contagem tem de voltar ao alvo, senao o desvio nao some: ele so
  // passa para o proximo movimento. Este e o coracao do pedido do
  // operador -- "saio de uma posicao e volto, e ela nao e mais a mesma".
  nota("contagem da junta 1: %ld passos (alvo %ld)",
       posicaoJ1(), grausParaPassos(J1, 20.0f));
  checar(labs(posicaoJ1() - grausParaPassos(J1, 20.0f)) < 5, "M01d",
         "e a contagem volta ao alvo: o desvio some, nao muda de lugar");

  // A prova do incomodo: sai e volta duas vezes, e o lugar tem de ser o
  // mesmo. Sem assentamento cada viagem deixaria um resto.
  irComPerda(5, 5, 0.4f);
  irComPerda(20, 10, 0.4f);
  nota("depois de sair e voltar: eixo em %.3f grau (alvo 20); %u retoque(s) -- \"%s\"",
       (double)eixoFisicoGraus(), (unsigned)correcaoResumo().tentativas,
       correcaoResumo().motivo);
  checar(fabsf(eixoFisicoGraus() - 20.0f) < 0.15f, "M01e",
         "sai da posicao, volta, e cai no mesmo lugar -- que era o pedido");
  checar(modoAtual == MODO_MANUAL, "M01f",
         "terminado o assentamento, o jog volta a ser do operador");
}

static void teste_M02_nao_retoca_quando_nao_deve() {
  secao("M02  Quando o assentamento NAO pode mexer no motor");
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoder(90, true, 0);
  rodarComWeb(300);
  enviarComando(CMD_ENCODER_ZERAR, 0);
  rodarComWeb(120);
  g_espelharEixo = true;

  // 1. Erro pequeno: ja esta bom, nao fica cutucando o eixo.
  irComPerda(15, 5, 0.02f);
  nota("perda de 0,02 grau: %u retoque(s) -- \"%s\"",
       (unsigned)correcaoResumo().tentativas, correcaoResumo().motivo);
  checar(correcaoResumo().tentativas == 0, "M02a",
         "perda dentro da tolerancia nao vira retoque: o eixo nao fica cutucando");

  // 2. Erro GRANDE: nao e folga. Empurrar o braco varios graus achando
  //    que esta consertando e o jeito mais rapido de bater a ferramenta.
  const long antes = (long)J1.motor->pulsosGerados;
  irComPerda(25, 5, 9.0f);
  const long depois = (long)J1.motor->pulsosGerados;
  const long previsto = labs(grausParaPassos(J1, 25.0f) - grausParaPassos(J1, 15.0f));
  nota("perda de 9 graus: estado %u -- \"%s\"", (unsigned)correcaoResumo().estado,
       correcaoResumo().motivo);
  nota("pulsos emitidos %ld (o movimento em si pedia ~%ld)",
       depois - antes, previsto);
  checar(correcaoResumo().estado == CORR_RECUSADA, "M02b",
         "erro grande demais NAO e corrigido: e denunciado");
  // Nenhum pulso ALEM do movimento pedido: o retoque nao aconteceu.
  checar(labs((depois - antes) - previsto) < 20, "M02c",
         "e nenhum pulso a mais sai no fio -- o braco nao anda por adivinhacao");

  // 3. Sem leitura do encoder: nao se move o braco no escuro.
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoder(90, true, 0);
  rodarComWeb(200);
  g_espelharEixo = true;
  g_uart.escravo[0].mudo = true;
  rodarComWeb(1500);
  const long antes2 = (long)J1.motor->pulsosGerados;
  irComPerda(12, 8, 0.0f);
  nota("cabo caido: estado %u -- \"%s\"; pulsos %ld",
       (unsigned)correcaoResumo().estado, correcaoResumo().motivo,
       (long)J1.motor->pulsosGerados - antes2);
  checar(correcaoResumo().estado == CORR_RECUSADA, "M02d",
         "sem leitura confiavel o assentamento recusa, em vez de adivinhar");
  checar(modoAtual == MODO_MANUAL, "M02e",
         "e o robo nao fica preso em POSICIONANDO por causa disso");
}

static void teste_M03_desligado_e_parada() {
  secao("M03  Desligar o assentamento, e a parada de emergencia");
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoder(90, true, 0);
  rodarComWeb(300);
  enviarComando(CMD_ENCODER_ZERAR, 0);
  rodarComWeb(120);
  g_espelharEixo = true;

  // Desligado, a maquina volta a se comportar como antes do encoder: a
  // perda fica na peca.
  configCorrecao.ativa = false;
  irComPerda(18, 6, 0.8f);
  nota("desligado: %u retoque(s); eixo em %.3f grau (alvo 18)",
       (unsigned)correcaoResumo().tentativas, (double)eixoFisicoGraus());
  checar(correcaoResumo().tentativas == 0, "M03a",
         "com o assentamento desligado nao ha retoque");
  checar(fabsf(eixoFisicoGraus() - 18.0f) > 0.5f, "M03b",
         "e a perda fica na peca -- que e como a maquina era antes do encoder");
  configCorrecao.ativa = true;

  // Parada de emergencia no meio do assentamento para o retoque tambem.
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoder(90, true, 0);
  rodarComWeb(300);
  enviarComando(CMD_ENCODER_ZERAR, 0);
  rodarComWeb(120);
  g_espelharEixo = true;
  webPost("/api/mover?t1=22&t2=9");
  rodarComWeb(60);
  perderPassos(1.0f);
  uint32_t t = 0;
  while (!correcaoEmCurso() && t < 8000) { rodarComWeb(20); t += 20; }
  const bool pegouEmCurso = correcaoEmCurso();
  solicitarParada();
  rodarComWeb(200);
  nota("pegou o assentamento em curso: %d;  depois da parada: em curso=%d",
       (int)pegouEmCurso, (int)correcaoEmCurso());
  checar(pegouEmCurso && !correcaoEmCurso(), "M03c",
         "a parada de emergencia cancela o retoque: nada anda depois do botao");
}

// ---------------------------------------------------------------------
// Um falso positivo aqui para o braco no meio de um cordao e estraga a
// peca. Este cenario e sobre o vigia NAO disparar -- que e a metade que
// decide se ele pode ficar ligado numa maquina que solda.
// ---------------------------------------------------------------------
static void teste_M04_travamento_nao_dispara_a_toa() {
  secao("M04  O vigia de travamento nao pode gritar sem motivo");
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoder(90, true, 0);
  rodarComWeb(300);
  enviarComando(CMD_ENCODER_ZERAR, 0);
  rodarComWeb(120);
  g_espelharEixo = true;

  // 1. Movimento normal, eixo acompanhando: nem um alarme.
  irComPerda(30, 10, 0.0f);
  nota("movimento normal: travamentos %lu",
       (unsigned long)correcaoTravamento().total);
  checar(!correcaoTravamento().ativo && correcaoTravamento().total == 0, "M04a",
         "movimento normal nao acusa travamento");

  // 2. Eixo parado nao pode acusar: eixo parado nao esta forcando nada.
  rodarComWeb(2000);
  checar(correcaoTravamento().total == 0, "M04b",
         "e eixo parado tambem nao -- parado nao esta forcando contra nada");

  // 3. Sem leitura do encoder, o vigia se cala. Um cabo solto no encoder
  //    nao pode parar o braco no meio de um cordao.
  g_espelharEixo = false;
  g_uart.escravo[0].mudo = true;
  rodarComWeb(1500);
  const long alvo = grausParaPassos(J1, 45.0f);
  moverCoordenado(alvo, posicaoJ2(), 30.0f);
  uint32_t t = 0;
  while (motoresEmMovimento() && t < 6000) { rodarComWeb(20); t += 20; }
  nota("cabo do encoder caido: travamentos %lu; o braco chegou em %.1f grau",
       (unsigned long)correcaoTravamento().total,
       (double)passosParaGraus(J1, posicaoJ1()));
  checar(correcaoTravamento().total == 0, "M04c",
         "sem leitura o vigia se cala: cabo solto no encoder nao para o braco");
  checar(fabsf(passosParaGraus(J1, posicaoJ1()) - 45.0f) < 1.0f, "M04d",
         "e o movimento chega ao fim normalmente");
}

// Religa a maquina COM o driver ja respondendo, que e o que acontece de
// verdade: o encoder nao aparece cinquenta milissegundos depois do boot.
// Configurar o escravo so depois do religamento deixava o firmware
// localizar-se em cima de valor de outro registrador.
static void religarComEncoder(int32_t bruto, bool mudo = false) {
  const NvsMock guardado = g_nvs;
  reiniciarSistema();
  // Nenhuma leitura anterior: num boot de verdade nao ha. Sem isto, os
  // 50 ms que reiniciarSistema() roda com o escravo ainda no padrao
  // deixam uma leitura VALIDA de outro registrador, e o firmware se
  // localiza em cima dela antes de o driver certo entrar no ar.
  //
  // Vem ANTES de carregar o NVS: e o NVS que traz a referencia absoluta,
  // e limpar depois dele apagaria justamente o que se quer restaurar.
  encoderReiniciarTeste();
  g_nvs = guardado;
  carregarConfiguracoes();

  g_uart.escravo[0] = EscravoModbus{};
  g_uart.escravo[0].id = 1;
  g_uart.escravo[0].funcao = 3;
  g_uart.escravo[0].regBase = 90;
  g_uart.escravo[0].baixaPrimeiro = true;
  g_uart.escravo[0].posicao = bruto;
  g_uart.escravo[0].mudo = mudo;
  // O driver 2 volta ao ar junto com o 1: religar a maquina nao tira um
  // driver do barramento, e sem ele o habilita (que agora e Modbus nos
  // dois) recusaria energizar depois do boot.
  g_uart.escravo[1] = EscravoModbus{};
  g_uart.escravo[1].id = 2;
  g_uart.escravo[1].mudo = mudo;
  g_uart.escravo[1].velocidade = 0;

  correcaoReiniciarTeste();
}

// =====================================================================
//  N - Zero absoluto: a maquina se localiza sozinha ao ligar
// =====================================================================
// O encoder do servo guarda a posicao com tudo desligado. Isso dispensa
// fim de curso -- mas so depois que alguem ENSINA qual contagem crua
// corresponde a zero grau. Antes disso a referencia e um numero
// arbitrario, e agir por ela poria o braco em qualquer lugar.
// ---------------------------------------------------------------------
static void teste_N01_ensinar_e_recuperar() {
  secao("N01  Ensinar o zero uma vez, e a maquina se acha ao ligar");
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoder(90, true, 500000);
  rodarComWeb(400);
  g_espelharEixo = false;

  nota("de fabrica: ensinado j1=%d j2=%d",
       (int)configZero.ensinado[0], (int)configZero.ensinado[1]);
  checar(!configZero.ensinado[0] && !configZero.ensinado[1], "N01a",
         "de fabrica o zero NAO vem ensinado: maquina nova liga como antes");

  // O operador leva o braco ao esquadro e diz: esta em 30 graus.
  const int cod = webPost("/api/zero/ensinar?j=1&g=30");
  rodarComWeb(200);
  nota("ensinando 30 graus: HTTP %d -- \"%s\"; contagem agora %.2f graus",
       cod, ultimaMensagem, (double)passosParaGraus(J1, posicaoJ1()));
  checar(cod == 200 && configZero.ensinado[0], "N01b",
         "ensinar grava a referencia absoluta daquela junta");
  checar(fabsf(passosParaGraus(J1, posicaoJ1()) - 30.0f) < 0.5f, "N01c",
         "e a contagem passa a valer o angulo ensinado, na hora");

  // O que importa: RELIGAR. O braco foi empurrado a mao com tudo
  // desligado -- o encoder ficou 10 graus adiante.
  const float porVolta = configEncoder.contagensPorVolta[0];
  const float red = (J1.reducao > 0.001f) ? J1.reducao : 1.0f;
  const int32_t dez = (int32_t)lroundf(10.0f * red / 360.0f * porVolta);
  const int32_t novoBruto = g_uart.escravo[0].posicao + dez;

  religarComEncoder(novoBruto);
  rodarComWeb(1200);

  nota("depois de religar com o braco movido a mao: contagem %.2f graus, estado %u",
       (double)passosParaGraus(J1, posicaoJ1()), (unsigned)zeroResumo().estado);
  checar(fabsf(passosParaGraus(J1, posicaoJ1()) - 40.0f) < 1.0f, "N01d",
         "ao ligar, a maquina LE onde o braco esta -- sem fim de curso e sem procurar batente");
  checar(zeroResumo().localizou[0], "N01e",
         "e diz que se localizou, em vez de fingir que o zero e onde ligou");
}

static void teste_N02_ir_ao_zero_ao_ligar() {
  secao("N02  Ir para 0 grau ao ligar, e quando NAO ir");
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoder(90, true, 500000);
  rodarComWeb(400);
  webPost("/api/zero/ensinar?j=1&g=25");
  rodarComWeb(200);

  // Religa com o braco fora do zero e SEM servos habilitados.
  religarComEncoder(500000);
  enviarComando(CMD_SERVOS, 0);
  rodarComWeb(1500);
  // Dagora o encoder acompanha o eixo de verdade, partindo de onde a
  // localizacao disse que ele estava. Sem isso o vigia de travamento
  // acusaria -- com razao: comando andando e medido parado.
  g_eixoBasePassos = posicaoJ1();
  g_espelharEixo   = true;

  nota("sem servos: estado %u, contagem %.2f graus, movendo=%d",
       (unsigned)zeroResumo().estado,
       (double)passosParaGraus(J1, posicaoJ1()), (int)motoresEmMovimento());
  checar(zeroResumo().estado == ZERO_LOCALIZADO, "N02a",
         "sem servos habilitados ele se localiza mas NAO anda");
  checar(!motoresEmMovimento(), "N02b",
         "o intertravamento e o proprio botao de servos, que e acao do operador");

  // O operador habilita os servos: AGORA o braco vai ao zero.
  enviarComando(CMD_SERVOS, 1);
  uint32_t t = 0;
  while (zeroResumo().estado != ZERO_PRONTO && t < 12000) { rodarComWeb(20); t += 20; }
  nota("depois de habilitar: estado %u, contagem %.2f graus -- \"%s\"",
       (unsigned)zeroResumo().estado,
       (double)passosParaGraus(J1, posicaoJ1()), zeroResumo().motivo);
  checar(fabsf(passosParaGraus(J1, posicaoJ1())) < 1.0f, "N02c",
         "habilitados os servos, o braco vai sozinho para 0 grau");
}

static void teste_N03_o_que_impede_de_ir() {
  secao("N03  O que impede a ida automatica ao zero");

  // 1. Zero nao ensinado: maquina nova nao pode sair andando.
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoder(90, true, 500000);
  const long antes = (long)J1.motor->pulsosGerados;
  rodarComWeb(2000);
  nota("zero nao ensinado: estado %u -- \"%s\"; pulsos %ld",
       (unsigned)zeroResumo().estado, zeroResumo().motivo,
       (long)J1.motor->pulsosGerados - antes);
  checar(zeroResumo().estado == ZERO_PRONTO &&
         (long)J1.motor->pulsosGerados - antes < 10, "N03a",
         "maquina sem zero ensinado nao anda sozinha: liga como antes");

  // 2. Sem leitura do encoder: nao trava a maquina, mas nao adivinha.
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoder(90, true, 500000);
  rodarComWeb(300);
  webPost("/api/zero/ensinar?j=1&g=25");
  rodarComWeb(200);
  religarComEncoder(500000, true);
  const long antes2 = (long)J1.motor->pulsosGerados;
  rodarComWeb(7000);
  nota("encoder mudo no boot: estado %u -- \"%s\"; pulsos %ld; modo %d",
       (unsigned)zeroResumo().estado, zeroResumo().motivo,
       (long)J1.motor->pulsosGerados - antes2, (int)modoAtual);
  checar(zeroResumo().estado == ZERO_SEM_ENCODER, "N03b",
         "sem leitura ele desiste e diz -- maquina que nao liga e pior que maquina desorientada");
  checar((long)J1.motor->pulsosGerados - antes2 < 10, "N03c",
         "e nao move o braco por adivinhacao");

  // 3. Esquecer o zero devolve a maquina ao jeito antigo.
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoder(90, true, 500000);
  rodarComWeb(300);
  webPost("/api/zero/ensinar?j=1&g=25");
  rodarComWeb(200);
  const int cod = webPost("/api/zero/esquecer?j=0");
  rodarComWeb(200);
  nota("esquecendo: HTTP %d, ensinado j1=%d", cod, (int)configZero.ensinado[0]);
  checar(cod == 200 && !configZero.ensinado[0], "N03d",
         "esquecer devolve a maquina ao jeito antigo, sem regravar firmware");
}

// =====================================================================
//  M05 - Seguir o eixo movido a mao, e quando NAO seguir
// =====================================================================
static void teste_M05_seguir_o_eixo_solto() {
  secao("M05  Braco movido a mao: quando a contagem segue e quando nao");
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoder(90, true, 500000);
  rodarComWeb(300);
  webPost("/api/zero/ensinar?j=1&g=0");
  rodarComWeb(200);

  // 1. Servos DESLIGADOS: o braco esta solto, e a contagem tem de seguir.
  enviarComando(CMD_SERVOS, 0);
  rodarComWeb(100);
  const float red = (J1.reducao > 0.001f) ? J1.reducao : 1.0f;
  const float cv  = configEncoder.contagensPorVolta[0];
  g_uart.escravo[0].parar();
  g_uart.escravo[0].posicao = encoderLer(1).referencia
                            + (int32_t)lroundf(12.0f * red / 360.0f * cv);
  rodarComWeb(400);
  nota("solto e empurrado ate 12 graus: contagem %.2f graus",
       (double)passosParaGraus(J1, posicaoJ1()));
  checar(fabsf(passosParaGraus(J1, posicaoJ1()) - 12.0f) < 0.5f, "M05a",
         "com o torque desligado a contagem segue o eixo movido a mao");

  // 2. Servos LIGADOS e a mesma divergencia: NAO pode seguir.
  //    Com torque, o motor esta segurando a posicao -- se o eixo saiu do
  //    lugar mesmo assim, isso e PERDA DE PASSO. Seguir a contagem ali
  //    esconderia o defeito, e o assentamento nunca traria o braco de
  //    volta: seria trocar uma correcao por um disfarce.
  enviarComando(CMD_SERVOS, 1);
  rodarComWeb(200);
  const float antes = passosParaGraus(J1, posicaoJ1());
  g_uart.escravo[0].parar();
  g_uart.escravo[0].posicao = encoderLer(1).referencia
                            + (int32_t)lroundf(20.0f * red / 360.0f * cv);
  rodarComWeb(600);
  nota("com torque, encoder pulou para 20 graus: contagem %.2f -> %.2f graus",
       (double)antes, (double)passosParaGraus(J1, posicaoJ1()));
  checar(fabsf(passosParaGraus(J1, posicaoJ1()) - antes) < 0.3f, "M05b",
         "com torque ligado a contagem NAO segue: divergencia ali e perda de passo, nao mao");

  // 3. Sem zero ensinado nao ha do que a leitura ser medida: nao segue.
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoder(90, true, 500000);
  rodarComWeb(300);
  enviarComando(CMD_SERVOS, 0);
  rodarComWeb(100);
  const float antes3 = passosParaGraus(J1, posicaoJ1());
  g_uart.escravo[0].parar();
  g_uart.escravo[0].posicao = 900000;
  rodarComWeb(500);
  nota("sem zero ensinado: contagem %.2f -> %.2f graus",
       (double)antes3, (double)passosParaGraus(J1, posicaoJ1()));
  checar(fabsf(passosParaGraus(J1, posicaoJ1()) - antes3) < 0.3f, "M05c",
         "sem zero ensinado a contagem fica quieta: a leitura crua nao e angulo");
}

// =====================================================================
//  P - O botao de aprendizado: ensinar o caminho com a mao
// =====================================================================
// O pedido do operador: segurar o botao solta o braco, ele leva a
// ponteira ate um canto e clica, leva ate o outro e clica. O programa
// nasce da peca.
//
// So funciona porque o encoder e absoluto: motor solto anda sem que
// nenhum pulso saia no fio, e sem o encoder a contagem ficaria parada --
// todos os pontos sairiam gravados no mesmo lugar.
// ---------------------------------------------------------------------

// Aperta e solta o botao com o tempo pedido, deixando o filtro de repique
// assentar dos dois lados.
static void botao(uint32_t msApertado) {
  g_pinEntrada[PIN_APRENDER] = LOW;
  rodarComWeb(msApertado);
  g_pinEntrada[PIN_APRENDER] = HIGH;
  rodarComWeb(120);
}

// Contato mecanico de verdade repica: alterna nivel por alguns
// milissegundos antes de assentar. Sem filtro, UM toque viraria varios
// pontos -- e o operador so descobriria isso na hora de soldar.
static void botaoComRepique(uint32_t msApertado) {
  for (int i = 0; i < 6; i++) {
    g_pinEntrada[PIN_APRENDER] = (i % 2) ? HIGH : LOW;
    rodarComWeb(3);
  }
  g_pinEntrada[PIN_APRENDER] = LOW;
  rodarComWeb(msApertado);
  for (int i = 0; i < 6; i++) {
    g_pinEntrada[PIN_APRENDER] = (i % 2) ? LOW : HIGH;
    rodarComWeb(3);
  }
  g_pinEntrada[PIN_APRENDER] = HIGH;
  rodarComWeb(120);
}

// Contagem crua do encoder correspondente a um angulo da junta k.
static int32_t brutoDe(uint8_t k, float graus) {
  const Junta& j = (k == 1) ? J1 : J2;
  const float red = (j.reducao > 0.001f) ? j.reducao : 1.0f;
  const float cv  = configEncoder.contagensPorVolta[k - 1];
  return encoderLer(k).referencia + (int32_t)lroundf(graus * red / 360.0f * cv);
}

// O braco sendo levado com a mao: o eixo vai para o angulo pedido e
// nenhum pulso sai no fio. E exatamente o que o driver desligado faz.
static void levarComAMao(float g1, float g2) {
  g_uart.escravo[0].parar();
  g_uart.escravo[1].parar();
  g_uart.escravo[0].posicao = brutoDe(1, g1);
  g_uart.escravo[1].posicao = brutoDe(2, g2);
  rodarComWeb(400);      // tempo de o seguidor ver e acertar a contagem
}

// As DUAS juntas no barramento, cada uma no seu endereco, e o zero
// absoluto ensinado nas duas. E o que o modo exige para soltar o braco.
static void prepararEncoderDasDuasJuntas() {
  for (uint8_t i = 0; i < 2; i++) {
    g_uart.escravo[i] = EscravoModbus{};
    g_uart.escravo[i].id = (uint8_t)(i + 1);
    g_uart.escravo[i].funcao = 3;
    g_uart.escravo[i].regBase = 90;
    g_uart.escravo[i].baixaPrimeiro = true;
    g_uart.escravo[i].posicao = 500000;
  }
  encoderPendente = configEncoder;
  encoderPendente.ativo         = true;
  encoderPendente.baud          = 19200;
  encoderPendente.paridade      = 0;
  encoderPendente.funcao        = 3;
  encoderPendente.periodoMs     = ENC_PERIODO_MIN_MS;
  encoderPendente.trintaEDois   = true;
  encoderPendente.baixaPrimeiro = true;
  encoderPendente.id[0]  = 1;  encoderPendente.id[1]  = 2;
  encoderPendente.reg[0] = 90; encoderPendente.reg[1] = 90;
  encoderPendente.contagensPorVolta[0] = 10000.0f;
  encoderPendente.contagensPorVolta[1] = 10000.0f;
  enviarComando(CMD_APLICAR_ENCODER);
  rodarComWeb(300);

  // O operador leva o braco ao esquadro e declara: as duas em 0 grau.
  webPost("/api/zero/ensinar?j=1&g=0");
  rodarComWeb(150);
  webPost("/api/zero/ensinar?j=2&g=0");
  rodarComWeb(150);
}

static void teste_P01_ensinar_com_a_mao() {
  secao("P01  Segurar o botao, levar a ponta com a mao, clicar em cada canto");
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoderDasDuasJuntas();
  rodarComWeb(300);

  nota("antes: modo aprendizado=%d, servos=%d, pontos=%u",
       (int)aprenderResumo().ativo, (int)servosLigados,
       (unsigned)progQuantidade());

  // 1. Segurar o botao.
  botao(APRENDER_SEGURAR_MS + 300);
  ResumoAprender a = aprenderResumo();
  nota("depois de segurar: ativo=%d solto=%d servos=%d -- \"%s\"",
       (int)a.ativo, (int)a.bracoSolto, (int)servosLigados, ultimaMensagem);
  checar(a.ativo, "P01a",
         "segurar o botao entra no modo aprendizado");
  checar(a.bracoSolto && !servosLigados, "P01b",
         "e o torque cai: o braco fica solto para o operador levar com a mao");

  // 2. Levar a ponta ate o primeiro canto -- com a mao, sem pulso nenhum.
  const long pulsosAntes = (long)J1.motor->pulsosGerados;
  levarComAMao(20.0f, -15.0f);
  nota("levado a mao ate 20/-15: contagem %.2f / %.2f graus; pulsos gerados %ld",
       (double)passosParaGraus(J1, posicaoJ1()),
       (double)passosParaGraus(J2, posicaoJ2()),
       (long)J1.motor->pulsosGerados - pulsosAntes);
  checar(fabsf(passosParaGraus(J1, posicaoJ1()) - 20.0f) < 0.5f &&
         fabsf(passosParaGraus(J2, posicaoJ2()) + 15.0f) < 0.5f, "P01c",
         "o sistema SABE onde a mao levou o braco -- o encoder acerta a contagem");
  checar((long)J1.motor->pulsosGerados - pulsosAntes == 0, "P01d",
         "e nenhum pulso saiu no fio: quem moveu foi a mao, nao o firmware");

  // 3. Clique: grava o ponto onde a ponta esta.
  botao(150);
  nota("primeiro clique: %u ponto(s) -- \"%s\"",
       (unsigned)progQuantidade(), ultimaMensagem);
  checar(progQuantidade() == 1, "P01e",
         "toque curto grava o ponto onde a ponta esta");

  // 4. Segundo canto, segundo clique.
  levarComAMao(-25.0f, 30.0f);
  botao(150);
  nota("segundo clique: %u ponto(s); ponto 2 em %.1f / %.1f graus",
       (unsigned)progQuantidade(),
       (double)passosParaGraus(J1, progLista()[1].p1),
       (double)passosParaGraus(J2, progLista()[1].p2));
  checar(progQuantidade() == 2, "P01f",
         "e o segundo toque grava o segundo ponto");
  checar(fabsf(passosParaGraus(J1, progLista()[1].p1) + 25.0f) < 0.6f &&
         fabsf(passosParaGraus(J2, progLista()[1].p2) - 30.0f) < 0.6f, "P01g",
         "o ponto gravado e onde a MAO deixou a ponta, nao onde o firmware achava");
  checar(fabsf(passosParaGraus(J1, progLista()[0].p1) - 20.0f) < 0.6f, "P01h",
         "e os dois pontos sao diferentes: o programa saiu do caminho de verdade");

  // 5. Segurar de novo: sai. O torque NAO volta sozinho.
  botao(APRENDER_SEGURAR_MS + 300);
  a = aprenderResumo();
  nota("depois de segurar de novo: ativo=%d servos=%d -- \"%s\"",
       (int)a.ativo, (int)servosLigados, ultimaMensagem);
  checar(!a.ativo, "P01i",
         "segurar de novo sai do modo aprendizado");
  checar(!servosLigados, "P01j",
         "e o torque NAO volta sozinho: habilitar servo continua sendo acao do operador");
}

static void teste_P02_um_toque_e_um_ponto() {
  secao("P02  Um toque e um ponto: repique de contato nao vira programa");
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoderDasDuasJuntas();
  rodarComWeb(300);
  botao(APRENDER_SEGURAR_MS + 300);
  levarComAMao(10.0f, 10.0f);

  botaoComRepique(150);
  nota("um toque com contato repicando: %u ponto(s)", (unsigned)progQuantidade());
  checar(progQuantidade() == 1, "P02a",
         "contato que repica ainda grava UM ponto so");

  // Toque sem sair do modo, com o braco parado no mesmo lugar: o ponto e
  // aceito de novo (dois pontos no mesmo lugar sao do operador decidir),
  // mas nao pode virar meia duzia.
  botaoComRepique(150);
  nota("segundo toque: %u ponto(s)", (unsigned)progQuantidade());
  checar(progQuantidade() == 2, "P02b",
         "e o toque seguinte grava exatamente mais um");
}

static void teste_P03_toque_fora_do_modo() {
  secao("P03  Toque fora do modo aprendizado nao pode gravar nada");
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoderDasDuasJuntas();
  rodarComWeb(300);

  botao(150);
  nota("toque com o modo desligado: %u ponto(s) -- \"%s\"",
       (unsigned)progQuantidade(), ultimaMensagem);
  checar(progQuantidade() == 0, "P03a",
         "botao encostado sem querer nao enche o programa de pontos");
  checar(strstr(ultimaMensagem, "Segure") != nullptr, "P03b",
         "e a mensagem ensina o gesto em vez de deixar o operador no escuro");

  // Botao PRESO no boot (fio em curto, botao emperrado) nao pode valer
  // como gesto: sem esta guarda a maquina entraria em aprendizado sozinha
  // e soltaria o braco na hora de ligar.
  g_pinEntrada[PIN_APRENDER] = LOW;
  reiniciarSistema();
  prepararRoboCalibrado();
  rodarComWeb(3000);
  nota("botao preso desde o boot: ativo=%d, servos=%d",
       (int)aprenderResumo().ativo, (int)servosLigados);
  checar(!aprenderResumo().ativo, "P03c",
         "botao preso no boot nao entra em aprendizado sozinho");
  g_pinEntrada[PIN_APRENDER] = HIGH;
}

static void teste_P04_sem_encoder_nao_solta_o_braco() {
  secao("P04  Sem encoder acompanhando, o braco NAO e solto");
  reiniciarSistema();
  prepararRoboCalibrado();
  // A bancada do operador hoje: so a junta 1 no barramento.
  prepararEncoder(90, true, 500000);
  rodarComWeb(300);
  webPost("/api/zero/ensinar?j=1&g=0");
  rodarComWeb(200);

  botao(APRENDER_SEGURAR_MS + 300);
  const ResumoAprender a = aprenderResumo();
  nota("junta 2 fora do barramento: ativo=%d solto=%d servos=%d -- \"%s\"",
       (int)a.ativo, (int)a.bracoSolto, (int)servosLigados, ultimaMensagem);
  checar(a.ativo, "P04a",
         "o modo entra assim mesmo: com torque ele funciona igual, so muda quem carrega o braco");
  checar(!a.bracoSolto && servosLigados, "P04b",
         "mas o torque NAO cai: junta que ninguem mede cairia e gravaria ponto torto");
  checar(strstr(ultimaMensagem, "torque") != nullptr, "P04c",
         "e a tela diz por que, em vez de o operador achar que o botao falhou");

  // Gravar continua funcionando -- e essa e a razao de nao recusar.
  botao(150);
  nota("toque com torque: %u ponto(s)", (unsigned)progQuantidade());
  checar(progQuantidade() == 1, "P04d",
         "e o toque grava ponto do mesmo jeito");
}

static void teste_P05_o_que_encerra_o_aprendizado() {
  secao("P05  O que encerra o aprendizado sem o operador pedir");

  // 1. Sair do modo manual.
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoderDasDuasJuntas();
  rodarComWeb(300);
  botao(APRENDER_SEGURAR_MS + 300);
  levarComAMao(10.0f, 10.0f);
  botao(150);
  levarComAMao(-10.0f, -10.0f);
  botao(150);

  enviarComando(CMD_SERVOS, 1);
  rodarComWeb(100);
  enviarComando(CMD_PROG_EXECUTAR, 1);       // ensaio
  rodarComWeb(200);
  nota("programa em execucao: modo=%d, aprendizado ativo=%d -- \"%s\"",
       (int)modoAtual, (int)aprenderResumo().ativo, ultimaMensagem);
  checar(!aprenderResumo().ativo, "P05a",
         "sair do modo manual encerra o aprendizado: ninguem ensina com o braco executando");

  // E o toque durante a execucao nao grava nada.
  const uint8_t antes = progQuantidade();
  botao(150);
  nota("toque com o programa rodando: %u -> %u ponto(s)",
       (unsigned)antes, (unsigned)progQuantidade());
  checar(progQuantidade() == antes, "P05b",
         "e um toque durante a execucao nao mexe no programa");

  // 2. Emergencia.
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoderDasDuasJuntas();
  rodarComWeb(300);
  botao(APRENDER_SEGURAR_MS + 300);
  checar(aprenderResumo().ativo, "P05c", "aprendizado ligado antes do botao vermelho");

  g_pinEntrada[PIN_ESTOP] = ESTOP_NIVEL_ATIVO;
  rodarComWeb(200);
  nota("emergencia: ativo=%d -- \"%s\"", (int)aprenderResumo().ativo, ultimaMensagem);
  checar(!aprenderResumo().ativo, "P05d",
         "o botao vermelho encerra o aprendizado junto com todo o resto");
  g_pinEntrada[PIN_ESTOP] = LOW;
  rodarComWeb(200);
}

// =====================================================================
//  P07 - O botao de emergencia com o fio partido
// =====================================================================
// A pergunta que decide se um botao de emergencia serve para alguma
// coisa: se o cabo dele romper, a maquina para ou continua andando?
//
// Com contato NC ligado ao GND e pull-up interno, fio partido e
// indistinguivel de botao apertado -- e e assim que tem de ser. Se
// alguem trocar a polaridade de novo, este cenario reprova.
// =====================================================================
//  V - O habilita (SON) pelo barramento
//
//  O SON deixou de ser fio. Isso trocou uma coisa que falhava sozinha
//  para o lado seguro por uma que nao falha para lado nenhum: RS485
//  rompido deixa o driver como estava. O que segue e a rede que se pos
//  no lugar -- toda escrita conferida relendo, e desabilitar que nao
//  confirma virando FALHA em vez de silencio.
// =====================================================================
static void teste_V01_habilita_pelo_barramento() {
  secao("V01  Habilitar e desabilitar pelo Modbus, com prova de releitura");
  reiniciarSistema();
  prepararRoboCalibrado();

  const uint16_t reg = configSon.reg;
  nota("registrador do habilita: %u (liga=%u, desliga=%u)",
       (unsigned)reg, (unsigned)configSon.valLiga, (unsigned)configSon.valDesliga);

  enviarComando(CMD_SERVOS, 1);
  rodarComWeb(60);
  const auto& d1 = g_uart.escravo[0].escritos;
  const auto& d2 = g_uart.escravo[1].escritos;
  const bool gravou1 = d1.count(reg) && d1.at(reg) == configSon.valLiga;
  const bool gravou2 = d2.count(reg) && d2.at(reg) == configSon.valLiga;
  nota("apos habilitar: servos=%d | driver 1 gravou=%d, driver 2 gravou=%d",
       (int)servosLigados, (int)gravou1, (int)gravou2);
  checar(servosLigados && gravou1 && gravou2, "V01a",
         "habilitar escreve o valor nos DOIS drivers, nao so no primeiro");

  enviarComando(CMD_SERVOS, 0);
  rodarComWeb(60);
  const bool zerou1 = d1.count(reg) && d1.at(reg) == configSon.valDesliga;
  const bool zerou2 = d2.count(reg) && d2.at(reg) == configSon.valDesliga;
  nota("apos desabilitar: servos=%d | driver 1=%u, driver 2=%u",
       (int)servosLigados,
       (unsigned)(d1.count(reg) ? d1.at(reg) : 9999),
       (unsigned)(d2.count(reg) ? d2.at(reg) : 9999));
  checar(!servosLigados && zerou1 && zerou2, "V01b",
         "desabilitar tambem vai nos dois: meio braco com torque e pior que nenhum");

  // Habilitar e desabilitar dao os dois SON_OK. Se a supervisao olhasse
  // o codigo de resultado em vez do pedido, esta sequencia nao teria
  // transicao para ver e a tela ficaria em "habilitado" com o braco solto.
  enviarComando(CMD_SERVOS, 1); rodarComWeb(60);
  const bool ligouDeNovo = servosLigados;
  enviarComando(CMD_SERVOS, 0); rodarComWeb(60);
  nota("liga -> desliga em sequencia: ligou=%d, desligou=%d",
       (int)ligouDeNovo, (int)!servosLigados);
  checar(ligouDeNovo && !servosLigados, "V01c",
         "dois pedidos seguidos com o mesmo resultado nao se confundem");
}

static void teste_V02_escrita_que_nao_confirma() {
  secao("V02  Driver que responde \"aceitei\" e guarda o valor velho");
  reiniciarSistema();
  prepararRoboCalibrado();

  // Parte do desabilitado: reescrever o valor que ja esta la confirmaria
  // sozinho, e o cenario nao provaria nada.
  enviarComando(CMD_SERVOS, 0);
  rodarComWeb(60);

  // O driver que MENTE. Existe de verdade: registrador so de leitura,
  // escrita bloqueada por nivel de acesso, parametro que so vale com o
  // servo parado. E o motivo de a escrita ser conferida relendo.
  g_uart.escravo[0].escritaMuda = true;
  enviarComando(CMD_SERVOS, 1);
  rodarComWeb(80);
  nota("driver que aceita e nao guarda: servos=%d -- \"%s\"",
       (int)servosLigados, ultimaMensagem);
  checar(!servosLigados, "V02a",
         "escrita sem releitura confirmada NAO conta como habilitado");
  checar(modoAtual != MODO_FALHA, "V02b",
         "habilitar que falha e so um comando que nao pegou: o braco segue sem torque");

  // Agora o caso grave: o eixo esta ENERGIZADO e o desabilita se perde.
  reiniciarSistema();
  prepararRoboCalibrado();
  enviarComando(CMD_SERVOS, 1);
  rodarComWeb(60);
  const bool estavaLigado = servosLigados;

  g_uart.escravo[0].escritaMuda = true;
  enviarComando(CMD_SERVOS, 0);
  rodarComWeb(80);
  nota("desabilitar perdido com o eixo energizado: modo=%d -- \"%s\"",
       (int)modoAtual, ultimaMensagem);
  checar(estavaLigado && modoAtual == MODO_FALHA, "V02c",
         "desabilitar que nao confirma derruba a maquina em FALHA");
  checar(!servosLigados, "V02d",
         "e o firmware nao segue dizendo que o braco esta habilitado");
}

static void teste_V03_barramento_mudo_e_sem_registrador() {
  secao("V03  Barramento mudo, e o registrador que ninguem configurou");
  reiniciarSistema();
  prepararRoboCalibrado();

  g_uart.escravo[0].mudo = true;
  g_uart.escravo[1].mudo = true;
  enviarComando(CMD_SERVOS, 1);
  rodarComWeb(200);
  nota("barramento mudo: servos=%d -- \"%s\"", (int)servosLigados, ultimaMensagem);
  checar(!servosLigados, "V03a",
         "sem resposta no fio a maquina nao se declara habilitada");

  // Sem registrador nao ha habilita. Dizer que habilitou seria a tela
  // mentindo sobre um braco que ninguem energizou.
  reiniciarSistema();
  prepararRoboCalibrado();
  // A propria rota recusa mexer no registrador com o braco energizado
  // (ver V05), entao o cenario tira o torque antes.
  enviarComando(CMD_SERVOS, 0);
  rodarComWeb(60);
  webPost("/api/son/config?reg=0&liga=1&desl=0&f16=0");
  rodarComWeb(20);
  const uint32_t antes = g_uart.escravo[0].escritas;
  enviarComando(CMD_SERVOS, 1);
  rodarComWeb(60);
  nota("registrador 0: servos=%d, escritas no fio=%lu -- \"%s\"",
       (int)servosLigados,
       (unsigned long)(g_uart.escravo[0].escritas - antes), ultimaMensagem);
  checar(!servosLigados, "V03b",
         "registrador 0 e \"nao configurado\": a maquina recusa habilitar");
  checar(g_uart.escravo[0].escritas == antes, "V03c",
         "e nao escreve em endereco nenhum -- o 0 e a tabela de parametros do driver");
}

static void teste_V04_driver_que_so_aceita_funcao_16() {
  secao("V04  Driver que recusa a funcao 06");
  reiniciarSistema();
  prepararRoboCalibrado();

  g_uart.escravo[0].soFuncao16 = true;
  g_uart.escravo[1].soFuncao16 = true;
  enviarComando(CMD_SERVOS, 1);
  rodarComWeb(80);
  nota("driver so-16 com o firmware na 06: servos=%d", (int)servosLigados);
  checar(!servosLigados, "V04a",
         "excecao na escrita nao vira habilitado por otimismo");

  // Nao manda desabilitar aqui: o braco nunca chegou a ter torque, e um
  // desabilita que tambem seria recusado cairia em FALHA -- e em FALHA a
  // rota de configuracao e recusada, que e justamente o que o operador
  // precisa alcancar para consertar. O caminho de saida existe (rearmar
  // limpa a FALHA), mas o cenario aqui e outro.
  webPost("/api/son/config?reg=98&liga=1&desl=0&f16=1");
  rodarComWeb(20);
  enviarComando(CMD_SERVOS, 1);
  rodarComWeb(80);
  nota("mesmo driver com a funcao 16 marcada: servos=%d", (int)servosLigados);
  checar(servosLigados, "V04b",
         "marcada a funcao 16 na tela, o mesmo driver passa a obedecer");
}

static void teste_V05_registrador_nao_muda_com_torque() {
  secao("V05  Trocar o registrador do habilita com o braco energizado");
  reiniciarSistema();
  prepararRoboCalibrado();
  enviarComando(CMD_SERVOS, 1);
  rodarComWeb(60);

  const uint16_t antes = configSon.reg;
  const int cod = webPost("/api/son/config?reg=120&liga=1&desl=0&f16=0");
  rodarComWeb(20);
  nota("troca com servos ligados: HTTP %d, registrador segue %u -- \"%s\"",
       cod, (unsigned)configSon.reg, ultimaMensagem);
  checar(cod != 200 && configSon.reg == antes, "V05a",
         "trocar o registrador com torque escreveria o desabilita no endereco "
         "novo e deixaria o antigo ligado, sem ninguem saber");
}

// ---------------------------------------------------------------------
// V06: o habilita nao pode SEQUESTRAR o barramento.
//
// A tarefa do encoder roda no core 0 com prioridade 2; a tarefa de rede
// (servidor web) roda no MESMO core com prioridade 1 -- menor. Uma
// escrita de habilita que faca 12 transacoes seguidas, cada uma podendo
// gastar ENC_TIMEOUT_MS sem resposta, prende o core por mais de um
// segundo. Nesse tempo o jog corta (TIMEOUT_JOG_MS = 350) e a leitura do
// encoder vence de idade (ENC_IDADE_MAX_MS = 1000).
//
// E o painel travando por causa de um botao -- exatamente o oposto do que
// habilitar servos deveria custar.
// ---------------------------------------------------------------------
static void teste_V06_habilita_nao_prende_o_barramento() {
  secao("V06  Habilitar nao pode travar o painel nem envelhecer a leitura");
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoder(90, true, 500000);
  rodarComWeb(400);

  // Driver mudo: o pior caso realista -- cabo solto, driver desligado,
  // endereco errado. E quando o operador MAIS aperta o botao.
  g_uart.escravo[0].mudo = true;
  g_uart.escravo[1].mudo = true;

  const uint32_t t0 = g_millis;
  enviarComando(CMD_SERVOS, 1);
  // Um unico ciclo do core 0 depois do comando: e onde a escrita acontece.
  rodarComWeb(1);
  const uint32_t gastoNoCiclo = g_millis - t0;

  nota("um ciclo com o barramento mudo custou %lu ms de relogio",
       (unsigned long)gastoNoCiclo);
  checar(gastoNoCiclo < TIMEOUT_JOG_MS, "V06a",
         "a tentativa de habilitar cabe dentro do prazo do jog: "
         "botao nao pode cortar o movimento de quem esta comandando");

  // E depois de tudo se resolver, a leitura tem de estar viva de novo.
  g_uart.escravo[0].mudo = false;
  g_uart.escravo[1].mudo = false;
  rodarComWeb(600);
  nota("leitura apos o episodio: valida=%d, idade=%lu ms",
       (int)encoderLer(1).valido, (unsigned long)encoderLer(1).idadeMs);
  checar(encoderLer(1).valido, "V06b",
         "e a leitura do encoder volta a valer: o habilita nao a mata de idade");
}

// ---------------------------------------------------------------------
// V07: a bancada real do operador -- UM driver no barramento.
//
// A versao anterior exigia que os dois confirmassem, entao habilitar
// recusava tudo dizendo que o driver 2 nao respondeu. Era verdade e nao
// ajudava ninguem: com o segundo driver ainda na caixa, nao dava para
// mexer no eixo que existe.
// ---------------------------------------------------------------------
static void teste_V07_um_driver_no_barramento() {
  secao("V07  Um driver so: habilitar o eixo que existe");
  reiniciarSistema();
  prepararRoboCalibrado();
  enviarComando(CMD_SERVOS, 0);
  rodarComWeb(80);

  // O driver 2 nao esta ligado. E o estado da bancada dele.
  g_uart.escravo[1].existe = false;

  enviarComando(CMD_SERVOS, 1, 1);          // so a junta 1
  rodarComWeb(120);
  nota("habilitando so a junta 1: J1=%d J2=%d, servosLigados=%d -- \"%s\"",
       (int)J1.habilitado, (int)J2.habilitado, (int)servosLigados, ultimaMensagem);
  checar(J1.habilitado && !J2.habilitado, "V07a",
         "com um driver no barramento, a junta 1 habilita sozinha");
  checar(!servosLigados, "V07b",
         "e a maquina NAO se declara pronta: movimento coordenado precisa das duas");

  // O jog da junta habilitada tem de andar. Era isto que estava travado.
  const long antes1 = posicaoJ1();
  for (int i = 0; i < 5; i++) { enviarComando(CMD_JOG, 1, 1); rodarComWeb(80); }
  enviarComando(CMD_JOG, 1, 0); rodarComWeb(300);
  nota("jog da junta 1: %ld -> %ld passos", antes1, posicaoJ1());
  checar(posicaoJ1() != antes1, "V07c",
         "e o jog dela anda: um eixo sem torque nao trava o eixo que tem");

  // O da outra, nao -- e diz por que.
  const long antes2 = posicaoJ2();
  for (int i = 0; i < 5; i++) { enviarComando(CMD_JOG, 2, 1); rodarComWeb(80); }
  enviarComando(CMD_JOG, 2, 0); rodarComWeb(300);
  nota("jog da junta 2 sem torque: %ld -> %ld passos -- \"%s\"",
       antes2, posicaoJ2(), ultimaMensagem);
  checar(posicaoJ2() == antes2, "V07d",
         "o eixo sem torque nao anda: o gerador de pulso contaria passos "
         "com o eixo parado e todo limite de curso passaria a mentir");

  // E desligar so ela funciona igual.
  enviarComando(CMD_SERVOS, 0, 1);
  rodarComWeb(120);
  nota("desabilitando so a junta 1: J1=%d J2=%d", (int)J1.habilitado, (int)J2.habilitado);
  checar(!J1.habilitado, "V07e",
         "desabilitar por junta tambem, sem depender do driver que nao existe");
}

// ---------------------------------------------------------------------
// V08: ir a 0 grau e operacao de INSTALACAO.
//
// Exigir calibracao ali era um ciclo fechado: para calibrar e preciso
// mover, e para mover era preciso calibrar. O jog ja rodava livre sem
// calibracao (modo de instalacao); o zero passou a seguir a mesma regra.
// ---------------------------------------------------------------------
static void teste_V08_zero_sem_calibracao() {
  secao("V08  Ir a 0 grau sem calibracao, e com um eixo so");
  reiniciarSistema();
  enviarComando(CMD_SERVOS, 1);
  rodarComWeb(120);

  // Maquina recem-montada: ninguem calibrou nada e o encoder ainda nao
  // foi configurado (registrador 0 = junta nao ligada). E o estado real
  // de quem acabou de parafusar o braco e quer leva-lo ao zero.
  J1.calibrada = J2.calibrada = false;
  configEncoder.reg[0] = configEncoder.reg[1] = 0;
  if (J1.motor) J1.motor->setCurrentPosition(grausParaPassos(J1, 20.0f));
  if (J2.motor) J2.motor->setCurrentPosition(grausParaPassos(J2, -15.0f));
  rodarComWeb(20);

  enviarComando(CMD_IR_HOME);
  rodarComWeb(60);
  nota("sem calibracao, ir ao zero: modo=%d -- \"%s\"", (int)modoAtual, ultimaMensagem);
  checar(modoAtual == MODO_POSICIONANDO, "V08a",
         "ir a 0 grau nao exige calibracao: e o que se faz para SAIR "
         "do estado nao calibrado");

  uint32_t t = 0;
  while (motoresEmMovimento() && t < 20000) { rodarComWeb(40); t += 40; }
  nota("chegou em %.2f / %.2f graus",
       (double)passosParaGraus(J1, posicaoJ1()),
       (double)passosParaGraus(J2, posicaoJ2()));
  checar(fabsf(passosParaGraus(J1, posicaoJ1())) < 1.0f &&
         fabsf(passosParaGraus(J2, posicaoJ2())) < 1.0f, "V08b",
         "e chega nos dois eixos");

  // E IR A UM PONTO GRAVADO tambem nao exige.
  //
  // O argumento antigo era que o ponto foi gravado num referencial
  // calibrado. Mas o ponto e um par de CONTAGENS, e a contagem nao muda
  // por a calibracao ter sido apagada: perseguir aquele par leva o braco
  // exatamente para onde ele estava quando o ponto foi gravado. O que se
  // perde sem calibracao e a protecao de curso -- e essa se perde de
  // qualquer jeito, tendo ou nao um ponto gravado.
  uint32_t tm = 0;
  while (modoAtual != MODO_MANUAL && tm < 5000) { rodarComWeb(40); tm += 40; }
  J1.calibrada = J2.calibrada = true;
  {
    const long p = (long)(90.0f * J1.passosPorGrau);
    J1.passosMin = -p; J1.passosMax = p;
    J2.passosMin = -p; J2.passosMax = p;
    recalcularResolucao();
  }
  rodarComWeb(10);
  enviarComando(CMD_PONTO_GRAVAR);
  rodarComWeb(40);
  const long alvoP1 = posicaoJ1();
  nota("pontos gravados: %u", (unsigned)progQuantidade());
  J1.calibrada = J2.calibrada = false;
  // Sai do ponto para haver caminho a percorrer.
  if (J1.motor) J1.motor->setCurrentPosition(alvoP1 - grausParaPassos(J1, 20.0f));
  rodarComWeb(10);
  enviarComando(CMD_IR_PARA_PONTO, 0);
  rodarComWeb(60);
  uint32_t tp = 0;
  while (motoresEmMovimento() && tp < 20000) { rodarComWeb(40); tp += 40; }
  nota("ponto gravado, perseguido sem calibracao: %ld passos (gravado em %ld)",
       posicaoJ1(), alvoP1);
  checar(labs(posicaoJ1() - alvoP1) < 40, "V08c",
         "e ir a um ponto GRAVADO tambem funciona sem calibracao: o ponto e "
         "uma contagem, e a contagem nao mudou");

  // Com um driver so: leva o eixo que tem torque, deixa o outro quieto.
  reiniciarSistema();
  enviarComando(CMD_SERVOS, 1, 1);          // so a junta 1
  rodarComWeb(120);
  J1.calibrada = J2.calibrada = false;
  configEncoder.reg[0] = configEncoder.reg[1] = 0;
  if (J1.motor) J1.motor->setCurrentPosition(grausParaPassos(J1, 20.0f));
  if (J2.motor) J2.motor->setCurrentPosition(grausParaPassos(J2, -15.0f));
  rodarComWeb(20);
  const long parada2 = posicaoJ2();

  enviarComando(CMD_IR_HOME);
  t = 0;
  rodarComWeb(60);
  while (motoresEmMovimento() && t < 20000) { rodarComWeb(40); t += 40; }
  nota("com so a junta 1 habilitada: J1 %.2f graus, J2 %ld -> %ld passos",
       (double)passosParaGraus(J1, posicaoJ1()), parada2, posicaoJ2());
  checar(fabsf(passosParaGraus(J1, posicaoJ1())) < 1.0f, "V08d",
         "o eixo com torque vai ao zero");
  checar(posicaoJ2() == parada2, "V08e",
         "e o eixo sem torque nao recebe pulso: contar passo de eixo parado "
         "faria todo limite de curso apontar para o lugar errado");
}

// ---------------------------------------------------------------------
// V09: com um driver so, desabilitar as duas juntas nao pode virar FALHA.
//
// A junta ausente nunca chegou a ser energizada por ninguem. Tratar o
// "nao confirmou" dela como o caso grave -- eixo possivelmente com
// torque e sem caminho para cortar -- derrubava a maquina em FALHA, e em
// FALHA todo comando e recusado, inclusive ir ao zero. Era o robo
// travando por causa de um motor que nao esta la.
// ---------------------------------------------------------------------
static void teste_V09_desabilitar_junta_ausente() {
  secao("V09  Desabilitar uma junta que nunca teve torque nao e falha");
  reiniciarSistema();
  prepararRoboCalibrado();
  enviarComando(CMD_SERVOS, 0);
  rodarComWeb(120);

  g_uart.escravo[1].existe = false;      // o driver 2 nao esta na bancada

  enviarComando(CMD_SERVOS, 1, 1);       // energiza so a junta 1
  rodarComWeb(120);
  checar(J1.habilitado && !J2.habilitado, "V09a",
         "a junta 1 energiza sozinha");

  // O botao "desabilitar servos" das duas: a junta 2 nao vai responder.
  enviarComando(CMD_SERVOS, 0, 0);
  rodarComWeb(200);
  nota("desabilitando as duas com o driver 2 fora: modo=%d, J1=%d J2=%d -- \"%s\"",
       (int)modoAtual, (int)J1.habilitado, (int)J2.habilitado, ultimaMensagem);
  checar(modoAtual != MODO_FALHA, "V09b",
         "nao e FALHA: a junta ausente nunca teve torque, e nao ha o que cortar");
  checar(!J1.habilitado && !J2.habilitado, "V09c",
         "e as duas ficam sem torque do mesmo jeito");

  // E o robo continua obedecendo -- que era o que estava travado.
  enviarComando(CMD_SERVOS, 1, 1);
  rodarComWeb(120);
  J1.calibrada = J2.calibrada = false;
  configEncoder.reg[0] = configEncoder.reg[1] = 0;
  if (J1.motor) J1.motor->setCurrentPosition(grausParaPassos(J1, 20.0f));
  rodarComWeb(20);
  enviarComando(CMD_IR_HOME);
  rodarComWeb(60);
  nota("ir ao zero depois disso: modo=%d -- \"%s\"", (int)modoAtual, ultimaMensagem);
  checar(modoAtual == MODO_POSICIONANDO, "V09d",
         "e ir ao zero continua funcionando com um motor so");

  // O caso GRAVE continua grave: junta que TINHA torque e cujo
  // desabilita nao confirmou. Ai sim nao se sabe se o eixo esta
  // energizado, e nao existe segundo caminho para cortar.
  reiniciarSistema();
  prepararRoboCalibrado();
  enviarComando(CMD_SERVOS, 1, 1);
  rodarComWeb(120);
  const bool tinhaTorque = J1.habilitado;
  g_uart.escravo[0].escritaMuda = true;
  enviarComando(CMD_SERVOS, 0, 1);
  rodarComWeb(200);
  nota("desabilitar perdido numa junta que TINHA torque: modo=%d -- \"%s\"",
       (int)modoAtual, ultimaMensagem);
  checar(tinhaTorque && modoAtual == MODO_FALHA, "V09e",
         "junta que estava energizada e cujo desabilita nao confirmou "
         "continua derrubando a maquina em FALHA");
}

// ---------------------------------------------------------------------
// V10: habilitar SO a junta 2. O espelho do V07, e nao e redundante:
// a junta 2 e a que anda por um caminho diferente na maquina de estados
// (comeca no indice 1 em vez de 0), entao um erro ali passaria batido
// num banco que so exercita a junta 1.
// ---------------------------------------------------------------------
static void teste_V10_habilitar_so_a_junta_2() {
  secao("V10  Habilitar so a junta 2");
  reiniciarSistema();
  prepararRoboCalibrado();
  enviarComando(CMD_SERVOS, 0);
  rodarComWeb(120);

  enviarComando(CMD_SERVOS, 1, 2);
  rodarComWeb(150);
  nota("habilitando so a junta 2: J1=%d J2=%d -- \"%s\"",
       (int)J1.habilitado, (int)J2.habilitado, ultimaMensagem);
  checar(J2.habilitado && !J1.habilitado, "V10a",
         "a junta 2 habilita sozinha, sem depender da 1");

  const long antes2 = posicaoJ2();
  for (int i = 0; i < 5; i++) { enviarComando(CMD_JOG, 2, 1); rodarComWeb(80); }
  enviarComando(CMD_JOG, 2, 0); rodarComWeb(300);
  nota("jog da junta 2: %ld -> %ld passos", antes2, posicaoJ2());
  checar(posicaoJ2() != antes2, "V10b", "e o jog dela anda");

  const long antes1 = posicaoJ1();
  for (int i = 0; i < 5; i++) { enviarComando(CMD_JOG, 1, 1); rodarComWeb(80); }
  enviarComando(CMD_JOG, 1, 0); rodarComWeb(300);
  checar(posicaoJ1() == antes1, "V10c",
         "e a junta 1, sem torque, continua parada");

  enviarComando(CMD_SERVOS, 0, 2);
  rodarComWeb(150);
  nota("desabilitando so a junta 2: J1=%d J2=%d", (int)J1.habilitado, (int)J2.habilitado);
  checar(!J2.habilitado, "V10d", "e desabilitar so ela tambem funciona");

  // As duas, uma de cada vez, tem de dar o mesmo que pedir as duas juntas.
  enviarComando(CMD_SERVOS, 1, 1); rodarComWeb(150);
  enviarComando(CMD_SERVOS, 1, 2); rodarComWeb(150);
  nota("uma de cada vez: J1=%d J2=%d, servosLigados=%d",
       (int)J1.habilitado, (int)J2.habilitado, (int)servosLigados);
  checar(J1.habilitado && J2.habilitado && servosLigados, "V10e",
         "habilitadas uma de cada vez, a maquina se declara pronta");
}

// ---------------------------------------------------------------------
// V11: a velocidade do posicionamento manual.
//
// Digitar um angulo e apertar o botao mandava o braco no deslocamento
// cheio, com o operador olhando de perto e sem jeito de pedir mais
// devagar. O botao Precisao ja existe e fica na mesma aba, logo acima
// dos campos -- ele so nao valia aqui.
// ---------------------------------------------------------------------
static void teste_V11_posicionar_respeita_precisao() {
  secao("V11  Ir para um angulo respeita o modo Precisao");
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoder(90, true, 500000);
  g_espelharEixo = false;
  rodarComWeb(200);

  // Mede quanto o eixo anda num tempo fixo, com e sem precisao. O que
  // importa nao e o numero e sim que um seja MUITO menor que o outro.
  const float alvo = 30.0f;
  auto percorrido = [&](bool precisao) -> long {
    enviarComando(CMD_IR_HOME);
    uint32_t t = 0;
    while (motoresEmMovimento() && t < 30000) { rodarComWeb(40); t += 40; }
    rodarComWeb(200);
    enviarComando(CMD_PRECISAO, precisao ? 1 : 0);
    rodarComWeb(40);
    const long de = posicaoJ1();
    webPost("/api/mover?t1=30&t2=0");
    rodarComWeb(600);                  // sempre o MESMO tempo de relogio
    const long quanto = labs(posicaoJ1() - de);
    enviarComando(CMD_PARAR);
    rodarComWeb(200);
    return quanto;
  };

  const long rapido = percorrido(false);
  const long lento  = percorrido(true);
  nota("em 600 ms: deslocamento %ld passos, precisao %ld passos (alvo %.0f graus)",
       rapido, lento, (double)alvo);
  checar(lento > 0 && rapido > lento * 2, "V11a",
         "com Precisao ligada o posicionamento anda bem mais devagar: "
         "o mesmo gesto que afina o jog afina o ir-para-angulo");

  // Deixa a maquina como a encontrou. reiniciarSistema() nao zera o modo
  // precisao, e um cenario que o deixasse ligado faria os seguintes
  // andarem a 2 graus/s -- eles esgotariam o tempo de espera e
  // reprovariam por um motivo que nao tem nada a ver com o que testam.
  enviarComando(CMD_PRECISAO, 0);
  rodarComWeb(40);
}

// ---------------------------------------------------------------------
// V12: leitura absurda do encoder nao pode ser tratada como confiavel.
//
// leituraPlausivel() abria mao de conferir quando a junta nao estava
// calibrada -- e maquina em comissionamento nunca esta. Com os dois
// encoders no barramento e um deles mal configurado (contagens por volta
// erradas, 32 bits errado, registrador do vizinho), o angulo saia em
// dezenas de milhares de graus, era marcado CONFIAVEL, ia para a tela e
// o braco desenhado girava sem parar.
//
// Nao existe junta desta maquina em 170 mil graus. Isso e conferivel sem
// calibracao nenhuma.
// ---------------------------------------------------------------------
static void teste_V12_leitura_absurda_nao_e_confiavel() {
  secao("V12  Angulo absurdo do encoder nao vira leitura boa");
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoderDasDuasJuntas();
  rodarComWeb(300);

  // Maquina em comissionamento: ninguem calibrou ainda.
  J1.calibrada = J2.calibrada = false;
  rodarComWeb(200);

  // A junta 2 passa a devolver uma contagem enorme -- e o que acontece
  // quando as contagens por volta ou o formato de 32 bits estao errados.
  g_uart.escravo[1].posicao = 500000000;
  rodarComWeb(400);

  const LeituraEncoder L2 = encoderLer(2);
  nota("junta 2 lendo %.0f graus, sem calibracao: confiavel=%d",
       (double)L2.graus, (int)leituraConfiavel(2));
  checar(fabsf(L2.graus) > 1000.0f, "V12a",
         "o cenario de fato produz um angulo absurdo");
  checar(!leituraConfiavel(2), "V12b",
         "angulo absurdo NAO e leitura confiavel, mesmo sem calibracao: "
         "sem isso ele ia para a tela e o braco desenhado girava sem parar");

  // E a junta boa continua valendo -- a guarda nao pode derrubar as duas.
  checar(leituraConfiavel(1), "V12c",
         "e a junta que le direito continua confiavel");

  // O status tambem tem de contar a verdade: m2ok e o que manda a tela
  // desenhar pelo medido em vez do comandado.
  webGet("/api/status");
  const std::string js = webCorpo();
  const size_t onde = js.find("\"m2ok\"");
  nota("status: %s", onde == std::string::npos ? "sem m2ok"
                                               : js.substr(onde, 13).c_str());
  checar(js.find("\"m2ok\":false") != std::string::npos, "V12d",
         "e o status diz false, para a tela desenhar pelo comandado");
}

// ---------------------------------------------------------------------
// V13: o angulo na tela tem de ser o do BRACO.
//
// A conversao antiga tira o angulo de dois numeros digitados -- contagens
// por volta do motor e reducao da engrenagem. Errar qualquer um sai em
// escala errada e nada denuncia: o braco em 90 graus mostra 47, ou 300.
//
// A escala ensinada e um numero so, medido na propria maquina: marque,
// leve o braco ate um angulo que voce CONHECE, diga quantos graus foram.
// ---------------------------------------------------------------------
static void teste_V14_velocidade_por_motor() {
  secao("V14  Cada motor na velocidade que ele aguenta");
  reiniciarSistema();
  prepararRoboCalibrado();

  // Junta 2 na metade da velocidade da junta 1.
  const int cod = webPost("/api/config?fvel1=1&fvel2=0.5");
  rodarComWeb(60);
  nota("fatores gravados: J1=%.2f J2=%.2f (HTTP %d)",
       (double)J1.fatorVel, (double)J2.fatorVel, cod);
  checar(fabsf(J1.fatorVel - 1.0f) < 0.01f &&
         fabsf(J2.fatorVel - 0.5f) < 0.01f, "V14a",
         "o fator de cada junta e gravado em separado");

  // Jog: no mesmo tempo, a junta 2 tem de andar perto da METADE.
  const long a1 = posicaoJ1(), a2 = posicaoJ2();
  for (int i = 0; i < 6; i++) {
    enviarComando(CMD_JOG, 1, 1); enviarComando(CMD_JOG, 2, 1);
    rodarComWeb(100);
  }
  enviarComando(CMD_JOG, 1, 0); enviarComando(CMD_JOG, 2, 0);
  rodarComWeb(400);
  const float g1 = fabsf(passosParaGraus(J1, posicaoJ1() - a1));
  const float g2 = fabsf(passosParaGraus(J2, posicaoJ2() - a2));
  nota("no mesmo tempo: junta 1 andou %.1f graus, junta 2 andou %.1f", (double)g1, (double)g2);
  checar(g1 > 1.0f && g2 > 0.1f && g2 < g1 * 0.75f, "V14b",
         "no jog, a junta com fator menor anda menos no mesmo tempo");

  // Movimento coordenado NAO pode ganhar fator por junta: as duas
  // deixariam de chegar junto e o caminho no espaco das juntas entortaria.
  // Ele anda no que a MAIS LENTA aguenta, e as duas chegam junto.
  webPost("/api/config?fvel1=1&fvel2=1");
  rodarComWeb(60);
  // Espera voltar ao MANUAL, nao so o eixo parar: CMD_MOVER_ANGULOS e
  // descartado em silencio fora do modo manual, e o modo demora alguns
  // ciclos a mais que o movimento.
  enviarComando(CMD_IR_HOME);
  rodarComWeb(120);            // deixa o comando PEGAR antes de esperar por ele
  uint32_t t = 0;
  while ((motoresEmMovimento() || modoAtual != MODO_MANUAL) && t < 20000)
    { rodarComWeb(40); t += 40; }
  enviarComando(CMD_MOVER_ANGULOS, 0, 0, 40.0f, 40.0f);
  uint32_t tCheio = 0;
  rodarComWeb(120);
  while (motoresEmMovimento() && tCheio < 30000) { rodarComWeb(40); tCheio += 40; }
  // Volta ao MANUAL antes de seguir: o IR_HOME de baixo e descartado em
  // silencio enquanto o modo ainda for POSICIONANDO.
  { uint32_t e = 0; while (modoAtual != MODO_MANUAL && e < 5000) { rodarComWeb(40); e += 40; } }
  const long f1 = posicaoJ1(), f2 = posicaoJ2();
  nota("fator 1: as duas chegaram em %lu ms, J1=%.1f J2=%.1f graus",
       (unsigned long)tCheio,
       (double)passosParaGraus(J1, f1), (double)passosParaGraus(J2, f2));
  checar(fabsf(passosParaGraus(J1, f1) - 40.0f) < 1.5f &&
         fabsf(passosParaGraus(J2, f2) - 40.0f) < 1.5f, "V14c",
         "com fator igual as duas chegam ao destino");

  // Agora com a junta 2 na metade: o movimento inteiro tem de demorar
  // MAIS, e as duas continuam chegando ao destino.
  enviarComando(CMD_IR_HOME);
  rodarComWeb(120);
  t = 0;
  while ((motoresEmMovimento() || modoAtual != MODO_MANUAL) && t < 20000)
    { rodarComWeb(40); t += 40; }
  webPost("/api/config?fvel1=1&fvel2=0.5");
  rodarComWeb(60);
  enviarComando(CMD_MOVER_ANGULOS, 0, 0, 40.0f, 40.0f);
  uint32_t tLento = 0;
  rodarComWeb(120);
  while (motoresEmMovimento() && tLento < 40000) { rodarComWeb(40); tLento += 40; }
  nota("com a junta 2 na metade: %lu ms (contra %lu), J1=%.1f J2=%.1f graus",
       (unsigned long)tLento, (unsigned long)tCheio,
       (double)passosParaGraus(J1, posicaoJ1()),
       (double)passosParaGraus(J2, posicaoJ2()));
  checar(tLento > tCheio, "V14d",
         "o movimento coordenado anda no que a junta mais lenta aguenta");
  checar(fabsf(passosParaGraus(J1, posicaoJ1()) - 40.0f) < 1.5f &&
         fabsf(passosParaGraus(J2, posicaoJ2()) - 40.0f) < 1.5f, "V14e",
         "e as duas continuam chegando junto ao destino: o caminho nao entorta");

  webPost("/api/config?fvel1=1&fvel2=1");
  rodarComWeb(60);
}

// ---------------------------------------------------------------------
// V15: NOMEAR UM ANGULO nao exige calibracao.
//
// "Ir para o zero" ja tinha sido liberado; "ir para um angulo" nao, e a
// diferenca nao tem razao de ser -- as duas sao a mesma frase, so muda o
// numero. Com o encoder dizendo onde a junta esta, mandar ela para 60
// graus e uma ordem completa sem calibracao nenhuma.
//
// Ir a um PONTO GRAVADO continua exigindo: aquele ponto foi gravado num
// referencial calibrado.
// ---------------------------------------------------------------------
static void teste_V15_ir_a_um_angulo_sem_calibracao() {
  secao("V15  Ir para um angulo nomeado, sem calibracao");
  reiniciarSistema();
  enviarComando(CMD_SERVOS, 1);
  rodarComWeb(150);

  J1.calibrada = J2.calibrada = false;
  configEncoder.reg[0] = configEncoder.reg[1] = 0;
  if (J1.motor) J1.motor->setCurrentPosition(grausParaPassos(J1, 83.0f));
  rodarComWeb(40);

  // Exatamente o caso relatado: eixo 1 em 83 graus, pedido 60.
  enviarComando(CMD_MOVER_ANGULOS, 0, 0, 60.0f, 0.0f);
  rodarComWeb(80);
  nota("de 83 para 60 graus sem calibracao: modo=%d -- \"%s\"",
       (int)modoAtual, ultimaMensagem);
  checar(modoAtual == MODO_POSICIONANDO, "V15a",
         "nomear um angulo nao exige calibracao: e a mesma frase do ir ao zero");

  uint32_t t = 0;
  while (motoresEmMovimento() && t < 30000) { rodarComWeb(40); t += 40; }
  nota("chegou em %.2f graus", (double)passosParaGraus(J1, posicaoJ1()));
  checar(fabsf(passosParaGraus(J1, posicaoJ1()) - 60.0f) < 1.0f, "V15b",
         "e chega no angulo pedido");
}

// ---------------------------------------------------------------------
// V16: quando a contagem para de descrever o braco, ela e reancorada.
//
// O painel do operador chegou a mostrar "comandado 1986,79 graus, medido
// -230,05, erro +2216,85". Isso nao e perda de passo -- nenhum braco
// perde dois mil graus. E a contagem tendo perdido o sentido porque o
// motor nao seguiu os pulsos. Continuar confiando nela e pior do que
// joga-la fora: todo limite de curso e todo destino passam a ser
// calculados sobre um numero que nao existe.
// ---------------------------------------------------------------------
static void teste_V16_contagem_perdida_e_reancorada() {
  secao("V16  Contagem que se perdeu volta a ser a do encoder");
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoderDasDuasJuntas();
  rodarComWeb(300);
  g_espelharEixo = false;

  // O encoder mede uma posicao, e a contagem de passos vai para MUITO
  // longe dela -- e o que acontece quando o motor nao segue os pulsos.
  const float medido = passosParaGraus(J1, posicaoJ1());
  if (J1.motor) J1.motor->setCurrentPosition(grausParaPassos(J1, medido + 600.0f));
  rodarComWeb(400);

  const float conta = passosParaGraus(J1, posicaoJ1());
  nota("contagem %.1f graus, encoder %.1f graus -- \"%s\"",
       (double)conta, (double)encoderLer(1).graus, ultimaMensagem);
  checar(fabsf(conta - encoderLer(1).graus) < 1.0f, "V16a",
         "acima do teto, a contagem e reescrita pelo encoder: e ele que "
         "sabe onde o braco esta");
  checar(strstr(ultimaMensagem, "reancorada") != nullptr, "V16b",
         "e a maquina DIZ que reescreveu -- mudar a posicao em silencio "
         "seria a tela trocando de numero sem ninguem entender por que");

  // Abaixo do teto NADA muda: divergencia pequena continua sendo perda
  // de passo, e continua sendo do assentamento.
  const float antes = passosParaGraus(J1, posicaoJ1());
  if (J1.motor) J1.motor->setCurrentPosition(grausParaPassos(J1, antes + 3.0f));
  rodarComWeb(400);
  nota("divergencia de 3 graus: contagem ficou em %.2f",
       (double)passosParaGraus(J1, posicaoJ1()));
  checar(fabsf(passosParaGraus(J1, posicaoJ1()) - (antes + 3.0f)) < 0.5f, "V16c",
         "divergencia pequena nao e reancorada: ela e perda de passo, e "
         "quem cuida dela e o assentamento");
}

// ---------------------------------------------------------------------
// V17: junta COM torque nao e seguida como se estivesse solta.
//
// O relato: "no campo mover, ir para angulo, quando eu coloco um angulo
// pra se mover, ele comeca a se mover mas dai ele da tipo uma
// atualizacao e dai para de se mover".
//
// A causa: o seguimento de eixo solto -- que existe para o operador
// empurrar o braco DESENERGIZADO com a mao e a contagem acompanhar --
// era travado por `servosLigados`, que so e verdade com AS DUAS juntas
// energizadas. Numa bancada com um driver so isso nunca acontece.
// Resultado: a contagem de uma junta COM torque era reescrita pelo
// encoder a cada ciclo, inclusive logo depois de um destino ter sido
// calculado a partir dela.
// ---------------------------------------------------------------------
static void teste_V17_junta_com_torque_nao_e_seguida() {
  secao("V17  Junta com torque nao e seguida como eixo solto");
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoderDasDuasJuntas();
  rodarComWeb(300);
  // O encoder segue o eixo de verdade, como na maquina.
  g_espelharEixo = true;

  // Um driver so: a junta 1 recebe torque, a 2 nao. E a bancada do
  // operador, e nela servosLigados (as duas) e falso.
  enviarComando(CMD_SERVOS, 0, 0);
  rodarComWeb(200);
  enviarComando(CMD_SERVOS, 1, 1);
  rodarComWeb(400);
  nota("junta 1 habilitada=%d, junta 2 habilitada=%d, servosLigados=%d",
       (int)J1.habilitado, (int)J2.habilitado, (int)servosLigados);
  checar(J1.habilitado && !J2.habilitado && !servosLigados, "V17a",
         "um driver so: a junta 1 tem torque, a maquina inteira nao");

  // O eixo fisico fica 4 graus atras da contagem -- perda de passo
  // comum, muito abaixo do teto de reancoragem, e o que qualquer braco
  // real acumula.
  perderPassos(4.0f);
  rodarComWeb(600);
  const float conta = passosParaGraus(J1, posicaoJ1());
  nota("contagem %.2f graus, encoder %.2f graus",
       (double)conta, (double)encoderLer(1).graus);
  checar(fabsf(encoderLer(1).graus - conta) > 2.0f, "V17b",
         "o eixo esta atras da contagem: e perda de passo, e quem cuida "
         "dela e o assentamento -- nao o seguimento de eixo solto");

  // E o movimento comandado chega onde foi mandado, em vez de arrancar e
  // morrer no primeiro ciclo de leitura.
  const uint32_t travAntes = correcaoTravamento().total;
  enviarComando(CMD_MOVER_ANGULOS, 0, 0, conta + 25.0f, 0.0f);
  rodarComWeb(120);
  uint32_t t = 0;
  while (motoresEmMovimento() && t < 30000) { rodarComWeb(40); t += 40; }
  nota("pedido %.1f graus, chegou em %.2f, travamentos %u -- \"%s\"",
       (double)(conta + 25.0f), (double)passosParaGraus(J1, posicaoJ1()),
       (unsigned)(correcaoTravamento().total - travAntes), ultimaMensagem);
  checar(fabsf(passosParaGraus(J1, posicaoJ1()) - (conta + 25.0f)) < 1.5f, "V17c",
         "e o angulo pedido e alcancado com um driver so no barramento");

  // Sem torque o seguimento volta a valer: e para isso que ele existe.
  enviarComando(CMD_SERVOS, 0, 1);
  rodarComWeb(400);
  const float agora = passosParaGraus(J1, posicaoJ1());
  g_espelharEixo = false;
  const float cv  = configEncoder.contagensPorVolta[0];
  const float red = (J1.reducao > 0.001f) ? J1.reducao : 1.0f;
  g_uart.escravo[0].parar();
  g_uart.escravo[0].posicao =
      encoderLer(1).referencia +
      (int32_t)lroundf((((agora - 6.0f) - J1.grausHome) * red / 360.0f) * cv);
  rodarComWeb(600);
  nota("sem torque, contagem foi para %.2f graus",
       (double)passosParaGraus(J1, posicaoJ1()));
  checar(fabsf(passosParaGraus(J1, posicaoJ1()) - (agora - 6.0f)) < 1.5f, "V17d",
         "desenergizada, a junta empurrada com a mao leva a contagem "
         "junto -- o modo continua existindo, so deixou de valer com torque");
  g_espelharEixo = false;
}

// ---------------------------------------------------------------------
// V18: o vigia de travamento nao para o braco por causa de numero de
// catalogo.
//
// Ele comparava a velocidade MEDIDA pelo encoder com uma esperada
// calculada de `pulsos por volta` x `contagens por volta` -- dois
// numeros digitados, nao medidos. Basta o driver estar configurado com
// outro numero de pulsos por volta para o esperado sair varias vezes
// maior que o real: meio segundo depois de arrancar, um braco andando
// normalmente e declarado travado e o movimento e cortado. E o mesmo
// desencontro faz o braco andar mais devagar do que o pedido -- foi
// assim que o defeito chegou: "esta muito lerdo, e quando mando ir a um
// angulo ele comeca a andar e para".
// ---------------------------------------------------------------------
static void teste_V18_vigia_usa_a_escala_medida() {
  secao("V18  Travamento julgado pela escala medida, nao pelo catalogo");
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoderDasDuasJuntas();
  rodarComWeb(300);
  g_espelharEixo = true;

  // A ESCALA MEDIDA e o que o encoder realmente ve por grau de junta --
  // e o que a calibracao guiada ensina.
  const float red = (J1.reducao > 0.001f) ? J1.reducao : 1.0f;
  const float cpgReal = configEncoder.contagensPorVolta[0] * red / 360.0f;
  configEncoder.contagensPorGrau[0] = cpgReal;

  // E o catalogo esta errado: o driver precisa de dez vezes mais pulsos
  // por volta do que esta escrito aqui.
  const uint32_t ppvVerdadeiro = J1.passosPorVolta;
  J1.passosPorVolta = ppvVerdadeiro / 10;
  rodarComWeb(100);

  const float esperadoCatalogo =
      1000.0f / (float)J1.passosPorVolta * configEncoder.contagensPorVolta[0];
  const float esperadoMedido =
      1000.0f / J1.passosPorGrau * fabsf(configEncoder.contagensPorGrau[0]);
  nota("a 1000 Hz de pulso: catalogo espera %.0f c/s, escala medida espera %.0f c/s",
       (double)esperadoCatalogo, (double)esperadoMedido);
  checar(esperadoCatalogo > esperadoMedido * 5.0f, "V18a",
         "o catalogo espera cinco vezes mais do que o eixo entrega -- "
         "e e essa conta que mandava parar o braco");

  const uint32_t travAntes = correcaoTravamento().total;
  const float parte = passosParaGraus(J1, posicaoJ1());
  enviarComando(CMD_MOVER_ANGULOS, 0, 0, parte + 30.0f,
                passosParaGraus(J2, posicaoJ2()));
  rodarComWeb(120);
  uint32_t t = 0;
  while (motoresEmMovimento() && t < 30000) { rodarComWeb(40); t += 40; }
  nota("chegou em %.2f graus, travamentos %u -- \"%s\"",
       (double)passosParaGraus(J1, posicaoJ1()),
       (unsigned)(correcaoTravamento().total - travAntes), ultimaMensagem);
  checar(correcaoTravamento().total == travAntes, "V18b",
         "braco andando de verdade nao e declarado travado por causa de "
         "um pulsos-por-volta que ninguem conferiu");
  checar(fabsf(passosParaGraus(J1, posicaoJ1()) - (parte + 30.0f)) < 1.5f, "V18c",
         "e o movimento chega ao fim, em vez de morrer no caminho");

  // E o travamento DE VERDADE continua sendo pego, pela mesma regua: o
  // comando anda, o eixo nao.
  g_espelharEixo = false;
  const float preso = passosParaGraus(J1, posicaoJ1());
  const int32_t travado = g_uart.escravo[0].posicao;
  moverCoordenado(grausParaPassos(J1, preso + 20.0f), posicaoJ2(), 20.0f);
  for (int k = 0; k < 400 && motoresEmMovimento(); k++) {
    g_uart.escravo[0].parar();
    g_uart.escravo[0].posicao = travado;   // eixo preso
    rodarComWeb(10);
  }
  rodarComWeb(200);
  nota("eixo preso com o comando andando: travamentos=%u -- \"%s\"",
       (unsigned)(correcaoTravamento().total - travAntes), ultimaMensagem);
  checar(correcaoTravamento().total > travAntes && !motoresEmMovimento(), "V18d",
         "eixo que nao responde ao pulso continua sendo acusado E parado, "
         "quando ha regua medida para julgar");

  // SEM regua medida o vigia AVISA, mas nao encosta no movimento.
  //
  // Parar o braco a partir de dois numeros digitados foi o que fazia a
  // maquina "travar as vezes": um pulsos-por-volta errado no driver e um
  // braco andando normalmente vira eixo travado meio segundo depois de
  // arrancar.
  correcaoLimparTravamento();
  configEncoder.contagensPorGrau[0] = 0.0f;
  const uint32_t travAntes2 = correcaoTravamento().total;
  const float preso2 = passosParaGraus(J1, posicaoJ1());
  const int32_t travado2 = g_uart.escravo[0].posicao;
  moverCoordenado(grausParaPassos(J1, preso2 + 30.0f), posicaoJ2(), 20.0f);
  // Noventa ciclos: o vigia dispara aos 500 ms e o movimento de 30 graus
  // a 20 graus/s so terminaria bem depois. Se o braco parar aqui, foi o
  // vigia que o parou.
  bool aindaAndando = true, avisou = false;
  for (int k = 0; k < 90; k++) {
    g_uart.escravo[0].parar();
    g_uart.escravo[0].posicao = travado2;   // eixo preso
    rodarComWeb(10);
    if (strstr(ultimaMensagem, "escala") != nullptr) avisou = true;
    if (!motoresEmMovimento()) { aindaAndando = false; break; }
  }
  nota("sem escala medida: travamentos=%u, ainda andando=%d, avisou=%d",
       (unsigned)(correcaoTravamento().total - travAntes2),
       (int)aindaAndando, (int)avisou);
  checar(aindaAndando && avisou, "V18e",
         "sem regua medida o vigia avisa e nao para o braco: cortar o "
         "movimento por causa de numero digitado e o que fazia a maquina "
         "travar do nada");

  pararSuave();
  rodarComWeb(200);
  J1.passosPorVolta = ppvVerdadeiro;
  configEncoder.contagensPorGrau[0] = 0.0f;
  correcaoLimparTravamento();
  g_espelharEixo = false;
}

// ---------------------------------------------------------------------
// V19: calibrar com os motores SOLTOS, empurrando o braco com a mao.
//
// "so preciso deixar livre os motores, mover ate o ponto maximo no eixo
// 1 positivo e depois o max negativo, e depois o mesmo com o eixo dois,
// isso e a calibracao, calculo automatico dai."
//
// Sem torque o gerador de pulso nao anda, e a contagem -- que e o que a
// marca grava -- ficaria parada nos quatro limites. Enquanto a
// calibracao esta aberta, cada junta sem torque tem a contagem puxada
// pelo encoder.
// ---------------------------------------------------------------------
static void teste_V19_calibrar_com_a_mao() {
  secao("V19  Calibrar com os motores soltos, empurrando com a mao");
  reiniciarSistema();
  prepararEncoderDasDuasJuntas();
  rodarComWeb(300);
  g_espelharEixo = false;

  // Motores soltos: e assim que se chega num batente sem bater.
  enviarComando(CMD_SERVOS, 0, 0);
  rodarComWeb(300);
  nota("torque: junta 1=%d, junta 2=%d",
       (int)J1.habilitado, (int)J2.habilitado);

  enviarComando(CMD_CALIB_INICIAR);
  rodarComWeb(200);
  checar(modoAtual == MODO_CALIBRANDO && estadoCalib == CAL_J1_POS, "V19a",
         "a calibracao abre sem exigir torque: o batente se alcanca com a "
         "mao, e era exatamente isso que a exigencia antiga proibia");

  // Empurrar o braco com a mao = mover o escravo do encoder. A contagem
  // tem de ir junto.
  const float cv  = configEncoder.contagensPorVolta[0];
  auto empurrar = [&](uint8_t k, float graus) {
    const Junta& j = (k == 1) ? J1 : J2;
    const float red = (j.reducao > 0.001f) ? j.reducao : 1.0f;
    g_uart.escravo[k - 1].parar();
    g_uart.escravo[k - 1].posicao +=
        (int32_t)lroundf((graus * red / 360.0f) * cv);
    rodarComWeb(300);
  };

  const long partiu = posicaoJ1();
  empurrar(1, +70.0f);
  nota("empurrado 70 graus com a mao: contagem andou %.1f graus",
       (double)((posicaoJ1() - partiu) / J1.passosPorGrau));
  checar(fabsf((posicaoJ1() - partiu) / J1.passosPorGrau - 70.0f) < 3.0f, "V19b",
         "com o motor solto a contagem e puxada pelo encoder: e a mao que "
         "move o braco, e a tela acompanha");

  enviarComando(CMD_CALIB_CONFIRMAR); rodarComWeb(120);
  empurrar(1, -110.0f);
  enviarComando(CMD_CALIB_CONFIRMAR); rodarComWeb(120);
  empurrar(2, +40.0f);
  enviarComando(CMD_CALIB_CONFIRMAR); rodarComWeb(120);
  empurrar(2, -100.0f);
  enviarComando(CMD_CALIB_CONFIRMAR); rodarComWeb(300);

  nota("J1 de %.1f a %.1f, J2 de %.1f a %.1f graus -- \"%s\"",
       (double)J1.grausMin, (double)J1.grausMax,
       (double)J2.grausMin, (double)J2.grausMax, ultimaMensagem);
  checar(modoAtual == MODO_MANUAL && estadoCalib == CAL_INATIVO, "V19c",
         "quatro marcas e acabou: nao ha etapa de volta, nem numero a digitar");
  checar(J1.calibrada && fabsf((J1.grausMax - J1.grausMin) - 110.0f) < 5.0f, "V19d",
         "o curso da junta 1 sai dos dois batentes, sem transferidor");
  checar(fabsf(J1.grausMin + J1.grausMax) < 2.0f, "V19e",
         "e o zero e o MEIO do curso: os limites saem simetricos sem "
         "ninguem declarar angulo nenhum");

  // A escala do encoder sai de graca da mesma medida.
  nota("escala medida na junta 1: %.2f contagens por grau",
       (double)configEncoder.contagensPorGrau[0]);
  const float esperada = cv * J1.reducao / 360.0f;
  checar(fabsf(configEncoder.contagensPorGrau[0] - esperada) < esperada * 0.05f,
         "V19f",
         "a escala do encoder sai da propria calibracao: entre as duas "
         "marcas ha um tanto de contagens e um tanto de graus");
}

// ---------------------------------------------------------------------
// V20: junta muda nao pode roubar o barramento a cada ciclo.
//
// "o sistema esta apresentando travamento as vezes." Numa bancada com um
// driver so -- o caso mais comum durante a montagem -- o ciclo do
// encoder alternava as duas juntas sempre, e metade das leituras era uma
// espera ate o timeout. Essa espera acontece na MESMA tarefa que divide
// o nucleo 0 com a rede: o sintoma nao e o motor, e a tela engasgando.
// ---------------------------------------------------------------------
static void teste_V20_junta_muda_nao_rouba_o_barramento() {
  secao("V20  Junta que nao responde deixa de ser perguntada toda vez");
  reiniciarSistema();
  prepararEncoderDasDuasJuntas();
  rodarComWeb(400);

  // A junta 2 some do barramento: driver desligado, cabo fora.
  g_uart.escravo[1].mudo = true;
  const uint32_t falhasAntes = encoderLer(2).falhas;
  const uint32_t okAntes     = encoderLer(1).leituras;
  rodarComWeb(4000);
  const uint32_t novasFalhas = encoderLer(2).falhas - falhasAntes;
  const uint32_t novasOk     = encoderLer(1).leituras - okAntes;

  nota("em 4 s: %u tentativa(s) na junta muda, %u leitura(s) boas na junta 1",
       (unsigned)novasFalhas, (unsigned)novasOk);
  checar(novasOk > 0, "V20a",
         "a junta que responde continua sendo lida");
  // Sem o recuo as duas seriam perguntadas em pe de igualdade: a muda
  // teria mais ou menos a mesma contagem da boa.
  checar(novasFalhas * 3 < novasOk, "V20b",
         "e a muda passa a ser perguntada de longe em longe, em vez de "
         "gastar metade do barramento esperando timeout");

  // Voltando a responder, ela volta ao ritmo normal na hora.
  g_uart.escravo[1].mudo = false;
  rodarComWeb(1500);
  nota("religada: leituras na junta 2 = %u", (unsigned)encoderLer(2).leituras);
  checar(encoderLer(2).leituras > 0, "V20c",
         "e o cabo voltando, a junta reaparece sozinha");
}

static void teste_P07_estop_a_prova_de_falha() {
  secao("P07  Emergencia: fio partido tem de parar a maquina");

  if (!ESTOP_FISICO_INSTALADO) {
    nota("compile com -DESTOP_FISICO_INSTALADO=true para exercitar este ramo");
    return;
  }

  reiniciarSistema();
  prepararRoboCalibrado();
  rodarComWeb(50);
  nota("botao instalado e solto (contato NC fechado, pino em LOW): servos=%d",
       (int)servosLigados);
  checar(servosLigados, "P07a",
         "botao solto e fio inteiro: a maquina opera normalmente");

  // O cabo rompe. Ninguem aperta nada -- o pull-up interno assume a
  // linha, e o pino sobe.
  g_pinEntrada[PIN_ESTOP] = HIGH;
  rodarComWeb(200);
  nota("fio partido: servos=%d, movimento liberado=%d -- \"%s\"",
       (int)servosLigados, (int)movimentoLiberado, ultimaMensagem);
  checar(!servosLigados && !movimentoLiberado, "P07b",
         "cabo rompido derruba torque e movimento: falha para o lado seguro");

  // E nao da para rearmar enquanto a linha estiver assim.
  enviarComando(CMD_SERVOS, 1);
  rodarComWeb(100);
  nota("tentativa de rearme com o cabo ainda partido: servos=%d -- \"%s\"",
       (int)servosLigados, ultimaMensagem);
  checar(!servosLigados, "P07c",
         "e nao da para religar o torque por cima de um botao que nao responde");

  // Cabo refeito: volta ao normal.
  g_pinEntrada[PIN_ESTOP] = LOW;
  rodarComWeb(150);
  enviarComando(CMD_SERVOS, 1);
  rodarComWeb(100);
  nota("cabo refeito: servos=%d", (int)servosLigados);
  checar(servosLigados, "P07d",
         "consertado o cabo, o rearme volta a funcionar");
}

static void teste_P06_pela_tela_tambem() {
  secao("P06  Quem nao instalou o botao usa a tela, e o estado aparece la");
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoderDasDuasJuntas();
  rodarComWeb(300);

  const int cod = webPost("/api/aprender?on=1");
  rodarComWeb(200);
  nota("POST /api/aprender?on=1 -> HTTP %d; ativo=%d solto=%d",
       cod, (int)aprenderResumo().ativo, (int)aprenderResumo().bracoSolto);
  checar(cod == 200 && aprenderResumo().ativo, "P06a",
         "da para entrar no aprendizado pela tela, sem botao fisico nenhum");

  webGet("/api/status");
  const std::string js = webCorpo();
  const size_t onde = js.find("\"aprBotao\"");
  nota("status: %s", js.substr(onde == std::string::npos ? 0 : onde, 62).c_str());
  checar(js.find("\"apr\":true") != std::string::npos &&
         js.find("\"aprSolto\":true") != std::string::npos, "P06b",
         "e o status diz que o braco esta solto -- a tela nao precisa adivinhar");

  // Gravar pela tela dentro do modo conta na sessao, igual ao botao.
  levarComAMao(12.0f, 12.0f);
  webPost("/api/ponto/gravar");
  rodarComWeb(200);
  nota("gravar pela tela: sessao=%u ponto(s), programa=%u",
       (unsigned)aprenderResumo().gravados, (unsigned)progQuantidade());
  checar(aprenderResumo().gravados == 1 && progQuantidade() == 1, "P06c",
         "gravar pela tela dentro do modo passa pelo mesmo caminho do botao");

  webPost("/api/aprender?on=0");
  rodarComWeb(200);
  checar(!aprenderResumo().ativo, "P06d", "e sair pela tela tambem funciona");
}

// =====================================================================
//  Q - Producao: pausa, contagem de pecas, desfazer, confirmacao do arco
// =====================================================================
// O que separa um braco de bancada de um equipamento de producao nao e
// precisao: e o que acontece na centesima peca. Pausar sem estragar o
// cordao, saber quantas ja sairam, desfazer um toque errado, e nao abrir
// arco por acidente.
// ---------------------------------------------------------------------

// Espera o programa terminar E o modo voltar para MANUAL. Esperar so por
// progRodando() nao basta: o modo so volta no ciclo seguinte, e o pedido
// seguinte cai em "Robo ocupado" -- que foi exatamente o que aconteceu
// aqui na primeira escrita deste cenario.
static bool esperarPrograma(uint32_t limiteMs = 60000) {
  uint32_t t = 0;
  while ((progRodando() || modoAtual != MODO_MANUAL) && t < limiteMs) {
    rodarComWeb(20); t += 20;
  }
  return !progRodando() && modoAtual == MODO_MANUAL;
}

// Programa minimo com um cordao, ja pronto para executar.
static void prepararProgramaDeSolda() {
  progLimpar();
  const char* m = nullptr;
  progAdicionarPonto(grausParaPassos(J1, 10.0f), grausParaPassos(J2, -20.0f), &m);
  progAdicionarPonto(grausParaPassos(J1, 20.0f), grausParaPassos(J2, -30.0f), &m);
  progDefinirSolda(0, true);
  rodarComWeb(20);
}

static void teste_Q01_pausar_no_meio_do_cordao() {
  secao("Q01  Pausar no meio de um cordao, e retomar de onde parou");
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararProgramaDeSolda();

  const int cod = webPost("/api/prog/executar?ensaio=0&conf=1");
  rodarComWeb(50);
  // Espera o cordao comecar de verdade.
  uint32_t t = 0;
  while (!soldaLigada() && t < 8000) { rodarComWeb(20); t += 20; }
  while (progFracaoTrecho() < 30 && t < 20000) { rodarComWeb(20); t += 20; }
  const uint8_t ondeParou = progFracaoTrecho();
  nota("executando com arco: HTTP %d, arco=%d, %u%% do cordao",
       cod, (int)soldaLigada(), (unsigned)ondeParou);
  checar(cod == 200 && soldaLigada() && ondeParou >= 30, "Q01a",
         "o cordao esta sendo percorrido com o arco aberto");

  webPost("/api/prog/pausar?on=1");
  rodarComWeb(400);
  nota("pausado: arco=%d, movendo=%d, guardado em %u%% -- \"%s\"",
       (int)soldaLigada(), (int)motoresEmMovimento(),
       (unsigned)progFracaoTrecho(), ultimaMensagem);
  checar(!soldaLigada(), "Q01b",
         "a pausa FECHA o arco: arco aberto com o braco parado fura a chapa");
  checar(progRodando() && progPausado(), "Q01c",
         "mas o programa continua vivo, so pausado");
  checar(progFracaoTrecho() >= 30, "Q01d",
         "e guarda em que altura do cordao parou");

  // Fica parado um tempo: nada pode andar, nada pode acender.
  const long p1Pausa = posicaoJ1();
  rodarComWeb(2000);
  nota("2 s pausado: andou %ld passos, arco=%d",
       posicaoJ1() - p1Pausa, (int)soldaLigada());
  checar(labs(posicaoJ1() - p1Pausa) < 5 && !soldaLigada(), "Q01e",
         "pausado e pausado: nada anda e o arco fica fechado");

  webPost("/api/prog/pausar?on=0");
  rodarComWeb(60);
  // O arco reabre com o tempo de abertura antes de voltar a andar.
  t = 0;
  while (!soldaLigada() && t < 3000) { rodarComWeb(20); t += 20; }
  nota("retomado: arco=%d, fase %u", (int)soldaLigada(), (unsigned)progFaseTeste());
  checar(soldaLigada(), "Q01f",
         "retomar reabre o arco -- a poca esfriou na pausa, arco frio nao funde");

  esperarPrograma();
  nota("fim: rodando=%d, arco=%d, pecas=%lu -- \"%s\"",
       (int)progRodando(), (int)soldaLigada(),
       (unsigned long)producao.ciclosTotais, ultimaMensagem);
  checar(!progRodando() && !soldaLigada(), "Q01g",
         "e o programa chega ao fim depois da pausa, com o arco fechado");
  checar(producao.ciclosTotais == 1, "Q01h",
         "a peca conta uma vez so, mesmo tendo sido pausada no meio");
}

static void teste_Q02_contagem_de_pecas() {
  secao("Q02  A contagem de pecas: o que conta e o que nao conta");
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararProgramaDeSolda();

  nota("maquina nova: %lu pecas", (unsigned long)producao.ciclosTotais);
  checar(producao.ciclosTotais == 0, "Q02a", "maquina nova comeca em zero");

  // 1. Ensaio NAO conta: nao gasta consumivel nem produz peca.
  const int codEns = webPost("/api/prog/executar?ensaio=1");
  rodarComWeb(100);
  nota("ensaio pedido: HTTP %d, modo %d, fase %u -- \"%s\"",
       codEns, (int)modoAtual, (unsigned)progFaseTeste(), ultimaMensagem);
  const bool acabou = esperarPrograma();
  nota("ensaio terminou=%d, modo %d, fase %u, J1 em %.1f graus",
       (int)acabou, (int)modoAtual, (unsigned)progFaseTeste(),
       (double)passosParaGraus(J1, posicaoJ1()));
  nota("depois de um ensaio: %lu pecas, %lu abortadas",
       (unsigned long)producao.ciclosTotais, (unsigned long)producao.abortados);
  checar(producao.ciclosTotais == 0 && producao.abortados == 0, "Q02b",
         "ensaio nao conta como peca -- nem como peca perdida");

  // 2. Execucao com arco que termina: conta.
  const int codArco = webPost("/api/prog/executar?ensaio=0&conf=1");
  rodarComWeb(100);
  nota("pedido de execucao com arco: HTTP %d, modo %d -- \"%s\"",
       codArco, (int)modoAtual, ultimaMensagem);
  esperarPrograma();
  nota("depois de uma execucao com arco: %lu pecas, arco aberto por %lu s",
       (unsigned long)producao.ciclosTotais, (unsigned long)producao.horasArcoS);
  checar(producao.ciclosTotais == 1, "Q02c", "peca pronta conta uma vez");
  checar(producao.horasArcoS > 0, "Q02d",
         "e o tempo de arco acumula -- e o numero que diz quando trocar bico");

  // 3. Parada no meio conta como ABORTADA, nao como peca.
  webPost("/api/prog/executar?ensaio=0&conf=1");
  rodarComWeb(400);
  webPost("/api/prog/parar");
  rodarComWeb(200);
  nota("interrompida no meio: %lu pecas, %lu abortadas",
       (unsigned long)producao.ciclosTotais, (unsigned long)producao.abortados);
  checar(producao.ciclosTotais == 1 && producao.abortados == 1, "Q02e",
         "peca pela metade nao e peca: entra como abortada");

  // 4. Sobrevive ao religamento: e um numero do dono da maquina.
  reiniciarSistemaMantendoNvs();
  nota("depois de religar: %lu pecas, %lu abortadas, %lu desde a manutencao",
       (unsigned long)producao.ciclosTotais, (unsigned long)producao.abortados,
       (unsigned long)producao.desdeManutencao);
  checar(producao.ciclosTotais == 1 && producao.abortados == 1, "Q02f",
         "e a contagem sobrevive a queda de energia");

  // 5. Registrar manutencao zera SO o contador de manutencao.
  const int cod = webPost("/api/manutencao/ok");
  rodarComWeb(200);
  nota("manutencao registrada: HTTP %d, total %lu, desde a manutencao %lu",
       cod, (unsigned long)producao.ciclosTotais,
       (unsigned long)producao.desdeManutencao);
  checar(cod == 200 && producao.desdeManutencao == 0 &&
         producao.ciclosTotais == 1, "Q02g",
         "registrar manutencao zera o contador dela e nao mexe no total da maquina");
}

static void teste_Q03_desfazer() {
  secao("Q03  Desfazer: o programa apagado por engano volta");
  reiniciarSistema();
  prepararRoboCalibrado();
  progLimpar();

  const char* m = nullptr;
  for (int i = 0; i < 5; i++)
    progAdicionarPonto(grausParaPassos(J1, 5.0f * i), grausParaPassos(J2, -10.0f), &m);
  rodarComWeb(20);
  nota("ensinados %u pontos", (unsigned)progQuantidade());
  checar(progQuantidade() == 5, "Q03a", "cinco pontos ensinados");

  // O estrago classico: apagar o programa inteiro.
  webPost("/api/prog/limpar");
  rodarComWeb(100);
  nota("depois de apagar: %u pontos, ha desfazer=%d -- \"%s\"",
       (unsigned)progQuantidade(), (int)progTemDesfazer(), ultimaMensagem);
  checar(progQuantidade() == 0 && progTemDesfazer(), "Q03b",
         "apagou -- e ha o que desfazer");

  const int cod = webPost("/api/prog/desfazer");
  rodarComWeb(100);
  nota("desfeito: HTTP %d, %u pontos -- \"%s\"",
       cod, (unsigned)progQuantidade(), ultimaMensagem);
  checar(cod == 200 && progQuantidade() == 5, "Q03c",
         "desfazer devolve os cinco pontos: meia hora de ensino nao se perde num toque");

  // Desfazer de novo volta ao estado anterior: um Ctrl+Z apertado duas
  // vezes sem querer nao pode deixar o operador pior do que comecou.
  webPost("/api/prog/desfazer");
  rodarComWeb(100);
  nota("desfazendo de novo: %u pontos", (unsigned)progQuantidade());
  checar(progQuantidade() == 0, "Q03d",
         "desfazer duas vezes volta ao que estava: a operacao e reversivel");

  // Remocao de um ponto tambem entra no desfazer.
  webPost("/api/prog/desfazer");
  rodarComWeb(100);
  webPost("/api/ponto/remover?i=2");
  rodarComWeb(100);
  const uint8_t depois = progQuantidade();
  webPost("/api/prog/desfazer");
  rodarComWeb(100);
  nota("remover um ponto e desfazer: %u -> %u -> %u",
       5u, (unsigned)depois, (unsigned)progQuantidade());
  checar(depois == 4 && progQuantidade() == 5, "Q03e",
         "remover um ponto por engano tambem se desfaz");

  // Nao da para desfazer com o programa rodando.
  prepararProgramaDeSolda();
  webPost("/api/prog/executar?ensaio=1");
  rodarComWeb(200);
  const int codR = webPost("/api/prog/desfazer");
  nota("desfazer com o programa rodando: HTTP %d", codR);
  checar(codR != 200, "Q03f",
         "desfazer com o braco executando e recusado antes de chegar ao core 1");
  webPost("/api/prog/parar");
  rodarComWeb(200);
}

static void teste_Q04_arco_exige_confirmacao() {
  secao("Q04  Abrir o arco exige confirmacao NA REQUISICAO");
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararProgramaDeSolda();

  // A tela pede dois toques. Isso nao protege nada se a rota abrir o arco
  // para qualquer chamada -- e ela e alcancavel por qualquer coisa na
  // rede da maquina.
  const int semConf = webPost("/api/prog/executar?ensaio=0");
  rodarComWeb(200);
  nota("executar com arco SEM conf=1: HTTP %d, modo %d, arco=%d",
       semConf, (int)modoAtual, (int)soldaLigada());
  checar(semConf != 200 && modoAtual != MODO_EXECUTANDO, "Q04a",
         "sem confirmacao explicita a rota recusa: nao abre arco por chamada solta");

  // Ensaio nao precisa: ele existe justamente para ser barato.
  const int ensaio = webPost("/api/prog/executar?ensaio=1");
  rodarComWeb(200);
  nota("ensaio sem conf: HTTP %d, modo %d", ensaio, (int)modoAtual);
  checar(ensaio == 200 && modoAtual == MODO_EXECUTANDO, "Q04b",
         "o ensaio nao exige confirmacao -- ele nao abre arco nenhum");
  webPost("/api/prog/parar");
  rodarComWeb(200);

  const int comConf = webPost("/api/prog/executar?ensaio=0&conf=1");
  rodarComWeb(200);
  nota("com conf=1: HTTP %d, modo %d", comConf, (int)modoAtual);
  checar(comConf == 200 && modoAtual == MODO_EXECUTANDO, "Q04c",
         "com a confirmacao, executa");
  webPost("/api/prog/parar");
  rodarComWeb(200);

  // Repetir tambem abre arco, entao tambem exige.
  const int rep = webPost("/api/prog/repetir");
  rodarComWeb(200);
  nota("repetir sem conf: HTTP %d", rep);
  checar(rep != 200, "Q04d",
         "\"mais uma peca\" tambem abre o arco, e tambem exige confirmacao");
}

static void teste_Q05_backup_leva_a_calibracao() {
  secao("Q05  O backup da maquina leva a calibracao junto");
  reiniciarSistema();
  prepararCartao();
  prepararRoboCalibrado();

  // Uma calibracao com numeros reconheciveis. Posta a mao de proposito:
  // o que este cenario mede e a ida e volta do arquivo, nao o assistente
  // -- que ja tem cenarios so dele.
  J1.passosMin = -8000; J1.passosMax = 9000; J1.grausHome = 12.5f;
  J2.passosMin = -7000; J2.passosMax = 7500; J2.grausHome = -3.5f;
  J1.calibrada = J2.calibrada = true;
  recalcularResolucao();
  rodarComWeb(30);
  const long min1 = J1.passosMin, max1 = J1.passosMax;
  const float home1 = J1.grausHome, home2 = J2.grausHome;
  nota("calibrado: J1 de %ld a %ld passos, referencia em %.2f graus",
       min1, max1, (double)home1);

  enviarComandoNomeado(CMD_ARQ_SALVAR_CONFIG, "maquina");
  esperarCartao();
  nota("backup: \"%s\"", armMensagem());
  checar(armEstado() == ARM_PRONTO, "Q05a", "o backup da maquina foi gravado");

  // Alguem refaz a calibracao errado -- ou apaga.
  calibApagar();
  J1.passosMin = 0; J1.passosMax = 0; J1.grausHome = 0.0f;
  J2.grausHome = 0.0f;
  rodarComWeb(50);
  checar(!J1.calibrada, "Q05b", "calibracao apagada");

  armSolicitar(TAR_CARREGAR_CONFIG, "maquina");
  esperarCartao();
  rodarComWeb(60);
  nota("restaurado: calibrada=%d, J1 de %ld a %ld, referencia %.2f / %.2f -- \"%s\"",
       (int)J1.calibrada, J1.passosMin, J1.passosMax,
       (double)J1.grausHome, (double)J2.grausHome, armMensagem());
  checar(J1.calibrada && J1.passosMin == min1 && J1.passosMax == max1, "Q05c",
         "o backup devolve o CURSO MEDIDO: antes so devolvia velocidades e elos");
  checar(fabsf(J1.grausHome - home1) < 0.01f &&
         fabsf(J2.grausHome - home2) < 0.01f, "Q05d",
         "e o angulo da referencia junto -- sem ele o desenho sai girado");
}

// Arquivo de backup gravado pela versao ANTERIOR nao tem calibracao. Ele
// nao pode zerar a que esta na maquina: quem restaura um backup velho
// espera recuperar o que ele guarda, nao perder o que ele nao guarda.
static void teste_Q06_backup_antigo_nao_apaga_calibracao() {
  secao("Q06  Backup de uma versao anterior nao apaga a calibracao viva");
  reiniciarSistema();
  prepararCartao();
  prepararRoboCalibrado();

  // Escreve a mao um arquivo no formato antigo: sem 'cal='.
  {
    File f = SD.open("/cfg/antigo.cfg", FILE_WRITE);
    f.println("ROBO2DOF-CFG 1");
    f.println("velN=25");
    f.println("velP=3");
    f.println("velA=15");
    f.println("velC=6.0");
    f.println("acel1=60");
    f.println("acel2=60");
    f.println("ppv1=10000");
    f.println("ppv2=10000");
    f.println("red1=16.5");
    f.println("red2=4.0");
    f.println("l1=450");
    f.println("l2=400");
    f.close();
  }
  J1.passosMin = -5500; J1.passosMax = 6100; J1.calibrada = true;
  recalcularResolucao();
  rodarComWeb(30);

  armSolicitar(TAR_CARREGAR_CONFIG, "antigo");
  esperarCartao();
  rodarComWeb(60);
  nota("apos carregar backup antigo: velN=%.0f, calibrada=%d, J1 de %ld a %ld",
       (double)velNormal, (int)J1.calibrada, J1.passosMin, J1.passosMax);
  checar(fabsf(velNormal - 25.0f) < 0.5f, "Q06a",
         "o backup antigo continua sendo aplicado no que ele traz");
  checar(J1.calibrada && J1.passosMin == -5500 && J1.passosMax == 6100, "Q06b",
         "e a calibracao viva fica intacta: o arquivo nao a traz, entao nao a apaga");
}

// =====================================================================
//  R01 - O SEGUNDO driver no barramento
// =====================================================================
// Ate aqui quase todo cenario de encoder rodou com um driver so, porque
// e a bancada do operador hoje. Isso deixa uma pergunta em aberto: o
// codigo trata a junta 2 de verdade, ou so a 1 esta ligada de fato?
//
// Aqui os dois estao no barramento, em enderecos diferentes, e tudo o
// que a junta 1 faz a junta 2 tem de fazer: ler, se localizar ao ligar,
// seguir o eixo movido a mao e ser assentada no fim do movimento.
static void teste_R01_o_segundo_driver() {
  secao("R01  Os dois drivers no mesmo barramento, cada um no seu endereco");
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoderDasDuasJuntas();
  rodarComWeb(400);

  const LeituraEncoder a1 = encoderLer(1);
  const LeituraEncoder a2 = encoderLer(2);
  nota("junta 1: valido=%d, %lu leituras, %lu falhas | junta 2: valido=%d, %lu leituras, %lu falhas",
       (int)a1.valido, (unsigned long)a1.leituras, (unsigned long)a1.falhas,
       (int)a2.valido, (unsigned long)a2.leituras, (unsigned long)a2.falhas);
  checar(a1.valido && a2.valido, "R01a",
         "os dois drivers respondem, cada um no seu endereco Modbus");
  checar(a1.leituras > 3 && a2.leituras > 3, "R01b",
         "e as duas juntas sao consultadas de verdade, nao so a primeira");

  // Uma pergunta para o endereco 2 nao pode ser respondida pelo 1: se
  // fosse, as duas juntas mostrariam o mesmo angulo e ninguem notaria
  // ate a peca sair torta.
  enviarComando(CMD_SERVOS, 0);
  rodarComWeb(100);
  levarComAMao(15.0f, -25.0f);
  nota("levadas a mao para 15 / -25: junta 1 le %.2f, junta 2 le %.2f",
       (double)encoderLer(1).graus, (double)encoderLer(2).graus);
  checar(fabsf(encoderLer(1).graus - 15.0f) < 0.5f &&
         fabsf(encoderLer(2).graus + 25.0f) < 0.5f, "R01c",
         "cada junta le o SEU angulo -- endereco trocado daria o mesmo numero nas duas");
  checar(fabsf(passosParaGraus(J2, posicaoJ2()) + 25.0f) < 0.5f, "R01d",
         "e o seguidor de eixo solto acerta a contagem da junta 2 tambem");

  // Assentamento na junta 2: o mesmo retoque que a 1 recebe.
  enviarComando(CMD_SERVOS, 1);
  // Com o zero absoluto ensinado, habilitar servos dispara a ida
  // automatica a 0 grau. Mandar um posicionamento por cima disso cai em
  // "Robo ocupado" -- espera-se a maquina se acomodar primeiro.
  {
    uint32_t z = 0;
    while ((zeroResumo().estado == ZERO_INDO || modoAtual != MODO_MANUAL) && z < 20000) {
      rodarComWeb(20); z += 20;
    }
  }
  // Dagora o eixo de verdade e espelhado nas duas juntas, partindo de
  // onde a contagem esta. Sem esta base os pulsos comecam em zero e o
  // encoder passa a discordar da contagem por um valor fixo.
  g_eixoBasePassos  = posicaoJ1() - (long)J1.motor->pulsosGerados;
  g_eixoBasePassos2 = posicaoJ2() - (long)J2.motor->pulsosGerados;
  g_espelharEixo    = true;
  rodarComWeb(300);

  // A junta 2 vai escorregar meio grau no caminho: perda de passo de
  // verdade, com o eixo fisico ficando para tras do comandado.
  perderPassos2(0.5f);

  // Pela porta normal de posicionamento: e ela que leva a maquina para
  // MODO_POSICIONANDO, e e so nesse modo que o loop roda o assentamento.
  // Chamar moverCoordenado() direto move o braco e nunca assenta.
  enviarComando(CMD_MOVER_ANGULOS, 0, 0,
                passosParaGraus(J1, posicaoJ1()), -10.0f);
  rodarComWeb(100);
  nota("pedido de posicionamento: modo %d, servos=%d -- \"%s\"",
       (int)modoAtual, (int)servosLigados, ultimaMensagem);
  uint32_t t = 0;
  while (modoAtual == MODO_POSICIONANDO && t < 20000) { rodarComWeb(20); t += 20; }
  const ResumoCorrecao rc = correcaoResumo();
  nota("modo ao fim: %d, junta 2 medida em %.2f graus (alvo -10,00)",
       (int)modoAtual, (double)encoderLer(2).graus);
  nota("assentamento da junta 2: estado %u, %u retoque(s), erro %.2f -> %.2f -- \"%s\"",
       (unsigned)rc.estado, (unsigned)rc.tentativas,
       (double)rc.erroInicial2, (double)rc.erroFinal2, rc.motivo);
  checar(rc.tentativas > 0 && fabsf(encoderLer(2).graus + 10.0f) < 0.2f, "R01e",
         "a junta 2 tambem e assentada pelo encoder: escorregou meio grau e o retoque trouxe de volta");

  // Um driver mudo nao pode derrubar a leitura do outro.
  g_uart.escravo[1].mudo = true;
  rodarComWeb(1200);
  nota("junta 2 muda: junta 1 valida=%d (idade %lu ms), junta 2 valida=%d",
       (int)encoderLer(1).valido, (unsigned long)encoderLer(1).idadeMs,
       (int)encoderLer(2).valido);
  checar(encoderLer(1).valido && !encoderLer(2).valido, "R01f",
         "driver mudo derruba SO a junta dele: um cabo solto nao cega o barramento inteiro");
}

// =====================================================================
//  S01 - Toda rota HTTP recebendo lixo
// =====================================================================
// As rotas sao alcancaveis por qualquer coisa na rede da maquina, e todo
// argumento delas chega como TEXTO. atof("abc") da 0, atoi("99999999999")
// estoura, e "-1" num indice de vetor le memoria que nao e nossa.
//
// Este cenario dispara cada rota registrada com valores hostis e exige
// tres coisas: nao travar, nao deixar a maquina num estado invalido, e
// nao mover o braco. Rodado tambem sob AddressSanitizer, ele e o que
// pega leitura fora de vetor -- que num ESP32 nao da erro nenhum, so
// devolve lixo e some.
// ---------------------------------------------------------------------
static const char* ROTAS_POST[] = {
  "/api/aprender", "/api/calib/apagar", "/api/calib/cancelar",
  "/api/calib/confirmar", "/api/calib/iniciar", "/api/config",
  "/api/config/reset", "/api/apagar/tudo", "/api/correcao", "/api/encoder/cacar",
  "/api/encoder/config", "/api/encoder/padroes", "/api/encoder/testar",
  "/api/encoder/zerar", "/api/geometria", "/api/gravar/iniciar",
  "/api/gravar/parar", "/api/home", "/api/jog", "/api/jogxy",
  "/api/manutencao/ok", "/api/mover", "/api/mover_xy", "/api/painel",
  "/api/parar", "/api/ponto/gravar", "/api/ponto/ir", "/api/ponto/remover",
  "/api/ponto/solda", "/api/precisao", "/api/prog/desenho",
  "/api/prog/desfazer", "/api/prog/executar", "/api/prog/limpar",
  "/api/prog/parar", "/api/prog/pausar", "/api/prog/repetir",
  "/api/protecoes", "/api/referenciar", "/api/reproduzir", "/api/sd/apagar",
  "/api/sd/carregar", "/api/sd/montar", "/api/sd/prever", "/api/sd/salvar",
  "/api/sentido", "/api/servos", "/api/solda", "/api/teste/rele",
  "/api/traj/limpar", "/api/travamento/ok", "/api/zero/config",
  "/api/zero/ensinar", "/api/zero/esquecer"
};
static const char* ROTAS_GET[] = {
  "/api/encoder", "/api/encoder/teste", "/api/pontos", "/api/rede",
  "/api/registro", "/api/saude", "/api/sd", "/api/sd/lista",
  "/api/sd/previa", "/api/status", "/api/trajetoria"
};

// Cada nome de argumento usado em qualquer rota, para nao depender de
// adivinhar qual rota le qual chave.
static const char* CHAVES[] = {
  "a","b","i","j","v","g","on","conf","ensaio","tipo","nome","senha",
  "atual","nova","l1","l2","dobra","envY","envR","velN","velP","velA",
  "velCordao","velC","acel1","acel2","ppv1","ppv2","red1","red2","suav",
  "escala","t1","t2","x","y","fx","fy","dir","junta","reg","reg1","reg2",
  "id1","id2","cv1","cv2","baud","par","func","per","b32","lo","ativo",
  "tol","max","alr","tent","vig","sin","ir","pts","n","de","ate","modo",
  "conf"
};

static const char* VALORES[] = {
  "", "0", "-1", "1", "255", "256", "-32769", "32768",
  "2147483647", "-2147483648", "4294967295", "99999999999999999999",
  "-99999999999999999999", "0.0", "-0.0", "nan", "inf", "-inf",
  "1e300", "-1e300", "1e-300", "abc", "0x10", "  ", "%%%",
  "../../etc/passwd", "..", "/", "\\", "a/b", "\"", "'", "<script>",
  "0,5", "1.7976931348623157e309", "000000000000001", "+5", "- 5",
  "1 2 3", "true", "false", "null", "9999999", "-9999999"
};

static void teste_S01_rotas_com_lixo() {
  secao("S01  Toda rota HTTP recebendo valores hostis");
  reiniciarSistema();
  prepararCartao();
  prepararRoboCalibrado();
  // Um programa e uma trajetoria de verdade, para as rotas de indice
  // terem em que errar.
  prepararProgramaDeSolda();
  rodarComWeb(50);

  const long p1Antes = posicaoJ1(), p2Antes = posicaoJ2();
  const float elo1Antes = elo1Mm, elo2Antes = elo2Mm;
  const long min1Antes = J1.passosMin, max1Antes = J1.passosMax;

  const size_t nP = sizeof(ROTAS_POST) / sizeof(ROTAS_POST[0]);
  const size_t nG = sizeof(ROTAS_GET)  / sizeof(ROTAS_GET[0]);
  const size_t nK = sizeof(CHAVES)     / sizeof(CHAVES[0]);
  const size_t nV = sizeof(VALORES)    / sizeof(VALORES[0]);

  uint32_t disparos = 0, aceitos = 0;
  char alvo[256];

  for (size_t v = 0; v < nV; v++) {
    for (size_t r = 0; r < nP; r++) {
      // Uma chave diferente por volta, para cobrir o produto sem
      // explodir o tempo: em nV voltas toda chave passa por toda rota.
      const char* k = CHAVES[(r + v) % nK];
      snprintf(alvo, sizeof(alvo), "%s?%s=%s", ROTAS_POST[r], k, VALORES[v]);
      const int cod = webPost(alvo);
      disparos++;
      if (cod == 200) aceitos++;
    }
    for (size_t r = 0; r < nG; r++) {
      const char* k = CHAVES[(r + v) % nK];
      snprintf(alvo, sizeof(alvo), "%s?%s=%s", ROTAS_GET[r], k, VALORES[v]);
      webGet(alvo);
      disparos++;
    }
    // Deixa o core 1 digerir o que entrou na fila antes da proxima leva.
    rodarComWeb(30);
  }

  nota("%lu requisicoes com valor hostil, %lu aceitas com HTTP 200",
       (unsigned long)disparos, (unsigned long)aceitos);
  checar(true, "S01a", "o sistema sobreviveu a varredura inteira sem travar");

  // A geometria e os limites sao a base de TODA protecao: se um valor
  // absurdo entrou neles, nenhuma recusa de movimento vale mais nada.
  nota("elos: %.1f/%.1f (eram %.1f/%.1f) | curso J1: %ld..%ld (era %ld..%ld)",
       (double)elo1Mm, (double)elo2Mm, (double)elo1Antes, (double)elo2Antes,
       J1.passosMin, J1.passosMax, min1Antes, max1Antes);
  checar(elo1Mm > 0.0f && elo2Mm > 0.0f &&
         elo1Mm < 100000.0f && elo2Mm < 100000.0f &&
         elo1Mm == elo1Mm && elo2Mm == elo2Mm, "S01b",
         "os comprimentos de elo continuam numeros finitos e positivos");
  checar(J1.passosMin < J1.passosMax && J2.passosMin < J2.passosMax, "S01c",
         "o curso das juntas continua coerente: min menor que max");
  checar(J1.passosPorGrau > 0.0f && J1.passosPorGrau == J1.passosPorGrau &&
         J2.passosPorGrau > 0.0f && J2.passosPorGrau == J2.passosPorGrau, "S01d",
         "a resolucao continua finita e positiva -- ela divide em meio mundo de conta");
  checar(velNormal > 0.0f && velAuto > 0.0f && velCordaoMmS > 0.0f &&
         velNormal == velNormal && velAuto == velAuto, "S01e",
         "as velocidades continuam finitas e positivas");

  const long andou1 = labs(posicaoJ1() - p1Antes), andou2 = labs(posicaoJ2() - p2Antes);
  nota("o braco andou %ld / %ld passos durante a varredura", andou1, andou2);
  checar(modoAtual != MODO_FALHA, "S01f",
         "e a maquina nao caiu em falha por causa de texto malformado");
}

// =====================================================================
//  S02 - Nome de arquivo hostil nao escapa da pasta
// =====================================================================
static void teste_S02_nomes_de_arquivo() {
  secao("S02  Nome de arquivo vindo da rede nao escapa da pasta");
  const char* RUINS[] = {
    "../segredo", "..", ".", "/etc/passwd", "a/../../b", "a\\b",
    "", " ", "  espaco na ponta ", "nome*com?curinga",
    "nome_muito_muito_muito_muito_muito_muito_muito_muito_longo_demais_para_caber",
    // "con" e "nul" ficam de fora de proposito: sao nomes reservados do
    // WINDOWS, nao do sistema de arquivos da maquina. Recusa-los aqui
    // seria inventar um problema que o ESP32 nao tem.
    "a:b", "a\"b", "a\nb"
  };
  uint32_t aceitos = 0;
  for (size_t i = 0; i < sizeof(RUINS) / sizeof(RUINS[0]); i++) {
    if (armNomeValido(RUINS[i])) {
      aceitos++;
      nota("ACEITOU: \"%s\"", RUINS[i]);
    }
  }
  nota("%lu de %u nomes hostis foram aceitos", (unsigned long)aceitos,
       (unsigned)(sizeof(RUINS) / sizeof(RUINS[0])));
  checar(aceitos == 0, "S02a",
         "nenhum nome hostil passa: barra, ponto-ponto e curinga sao recusados");

  const char* BONS[] = { "peca 1", "cantoneira-30", "chapa_2mm", "A", "teste 123" };
  uint32_t recusados = 0;
  for (size_t i = 0; i < sizeof(BONS) / sizeof(BONS[0]); i++)
    if (!armNomeValido(BONS[i])) { recusados++; nota("RECUSOU: \"%s\"", BONS[i]); }
  checar(recusados == 0, "S02b",
         "e os nomes normais continuam passando -- guarda que recusa tudo nao serve");
}

// =====================================================================
//  T01 - Uma leitura de encoder ABSURDA nao pode virar posicao
// =====================================================================
// O encoder e a unica testemunha de onde o braco esta. Se ele mentir uma
// vez -- registrador errado, contagens por volta erradas, ruido que
// passou no CRC -- o firmware escreve essa mentira na contagem de passos,
// e a partir dali TODA protecao de curso se apoia num numero inventado.
//
// O caso que assusta e o boot: a maquina se localiza sozinha por UMA
// leitura e, se o operador habilitar os servos, vai para "0 grau"
// partindo de onde ela acha que esta. Achando que esta a 300 graus, ela
// manda 300 graus de pulso contra o batente.
//
// A defesa nao e estatistica, e fisica: o braco nao PODE estar fora do
// curso que o proprio operador mediu. Leitura fora dali nao e posicao, e
// defeito -- e defeito se denuncia, nao se obedece.
// ---------------------------------------------------------------------
static void teste_T01_leitura_absurda() {
  secao("T01  Leitura de encoder fora do curso nao vira posicao");

  // ---- 1. no boot ----
  reiniciarSistema();
  prepararRoboCalibrado(90.0f);          // curso +/-90 graus
  prepararEncoder(90, true, 500000);
  rodarComWeb(300);
  webPost("/api/zero/ensinar?j=1&g=0");
  rodarComWeb(200);

  // O driver passa a responder um valor que corresponde a ~300 graus:
  // impossivel num braco com curso de +/-90.
  const float red = (J1.reducao > 0.001f) ? J1.reducao : 1.0f;
  const float cv  = configEncoder.contagensPorVolta[0];
  const int32_t absurdo = encoderLer(1).referencia
                        + (int32_t)lroundf(300.0f * red / 360.0f * cv);

  religarComEncoder(absurdo);
  rodarComWeb(1500);
  const float lido  = encoderLer(1).graus;
  const float conta = passosParaGraus(J1, posicaoJ1());
  nota("encoder diz %.1f graus; curso calibrado vai de %.1f a %.1f",
       (double)lido, (double)J1.grausMin, (double)J1.grausMax);
  nota("contagem apos o boot: %.1f graus; estado do zero: %u -- \"%s\"",
       (double)conta, (unsigned)zeroResumo().estado, zeroResumo().motivo);
  checar(fabsf(conta) < 95.0f, "T01a",
         "leitura fora do curso NAO e escrita na contagem: o braco nao pode estar la");
  checar(!zeroResumo().localizou[0], "T01b",
         "e a maquina nao se declara localizada por cima de uma leitura impossivel");

  // Habilitar os servos nao pode disparar uma viagem de 300 graus.
  const long antes = (long)J1.motor->pulsosGerados;
  enviarComando(CMD_SERVOS, 1);
  rodarComWeb(4000);
  const long pulsos = (long)J1.motor->pulsosGerados - antes;
  nota("depois de habilitar os servos: %ld pulsos gerados (%.1f graus)",
       pulsos, (double)(pulsos / (J1.passosPorGrau > 0 ? J1.passosPorGrau : 1)));
  checar(fabsf(pulsos / (J1.passosPorGrau > 0 ? J1.passosPorGrau : 1)) < 95.0f, "T01c",
         "e o braco nao sai andando um curso inteiro contra o batente");

  // ---- 2. com o braco solto ----
  reiniciarSistema();
  prepararRoboCalibrado(90.0f);
  prepararEncoder(90, true, 500000);
  rodarComWeb(300);
  webPost("/api/zero/ensinar?j=1&g=0");
  rodarComWeb(200);
  enviarComando(CMD_SERVOS, 0);
  rodarComWeb(200);

  const float antesSolto = passosParaGraus(J1, posicaoJ1());
  g_uart.escravo[0].parar();
  g_uart.escravo[0].posicao = encoderLer(1).referencia
                            + (int32_t)lroundf(300.0f * red / 360.0f * cv);
  rodarComWeb(600);
  nota("braco solto, leitura pula para 300 graus: contagem %.1f -> %.1f",
       (double)antesSolto, (double)passosParaGraus(J1, posicaoJ1()));
  checar(fabsf(passosParaGraus(J1, posicaoJ1()) - antesSolto) < 1.0f, "T01d",
         "o seguidor de eixo solto tambem recusa: mao nenhuma leva o braco para fora do curso");

  // ---- 3. leitura DENTRO do curso continua funcionando ----
  g_uart.escravo[0].posicao = encoderLer(1).referencia
                            + (int32_t)lroundf(35.0f * red / 360.0f * cv);
  rodarComWeb(600);
  nota("leitura plausivel de 35 graus: contagem agora %.2f",
       (double)passosParaGraus(J1, posicaoJ1()));
  checar(fabsf(passosParaGraus(J1, posicaoJ1()) - 35.0f) < 0.5f, "T01e",
         "e leitura dentro do curso continua sendo obedecida -- a guarda nao pode cegar o encoder");
}

// =====================================================================
//  T02 - Mensagem com aspas nao pode quebrar o JSON
// =====================================================================
// As mensagens da maquina viajam DENTRO de JSON, e varias delas trazem
// aspas: `programa "peca 1" salvo`. Sem escapar, a resposta sai assim:
//
//     {"msg":"programa "peca 1" salvo"}
//
// que nao e JSON. O r.json() do navegador lanca excecao, o contador de
// quedas sobe e a interface anuncia "sem comunicacao" -- com a maquina
// funcionando perfeitamente. E acontece na acao mais comum que existe:
// salvar ou carregar um arquivo do cartao.
// ---------------------------------------------------------------------

// Analisador de JSON de verdade, pequeno mas ESTRITO.
//
// Contar aspas nao serve: `{"msg":"programa "peca 1" salvo"}` tem numero
// PAR de aspas e passa em qualquer conferencia frouxa -- e nao e JSON.
// Foi exatamente assim que este defeito sobreviveu a primeira versao
// deste cenario. Aqui a gramatica e seguida de verdade: depois de um
// valor so pode vir virgula ou o fecha-chaves.
namespace mini {

struct P {
  const char* s; size_t n; size_t i; bool ok;
  P(const std::string& t) : s(t.c_str()), n(t.size()), i(0), ok(true) {}
  void espaco() { while (i < n && (s[i]==' '||s[i]=='\t'||s[i]=='\n'||s[i]=='\r')) i++; }
  bool fim() const { return i >= n; }
  char ve() const { return i < n ? s[i] : '\0'; }
  bool come(char c) { espaco(); if (ve()==c) { i++; return true; } return false; }
  void erro() { ok = false; }

  void texto() {
    if (!come('"')) { erro(); return; }
    while (i < n) {
      const char c = s[i++];
      if (c == '"') return;
      if (c == '\\') {
        if (i >= n) { erro(); return; }
        const char e = s[i++];
        if (e=='"'||e=='\\'||e=='/'||e=='b'||e=='f'||e=='n'||e=='r'||e=='t') continue;
        if (e=='u') { for (int k=0;k<4 && i<n;k++) i++; continue; }
        erro(); return;                      // escape que nao existe
      }
      if ((unsigned char)c < 0x20) { erro(); return; }   // controle cru
    }
    erro();                                  // texto sem fechar
  }

  void numero() {
    const size_t ini = i;
    if (ve()=='-'||ve()=='+') i++;
    while (i<n && ((s[i]>='0'&&s[i]<='9')||s[i]=='.'||s[i]=='e'||s[i]=='E'||
                   s[i]=='-'||s[i]=='+')) i++;
    if (i == ini) { erro(); return; }
    // "nan" e "inf" nao existem em JSON, e sao o que um float estragado
    // produz -- entao eles caem no ramo de palavra, abaixo, e reprovam.
  }

  void palavra(const char* p) {
    const size_t L = strlen(p);
    if (i + L <= n && strncmp(s + i, p, L) == 0) i += L; else erro();
  }

  void valor() {
    espaco();
    if (!ok || fim()) { erro(); return; }
    const char c = ve();
    if (c=='"') texto();
    else if (c=='{') objeto();
    else if (c=='[') lista();
    else if (c=='t') palavra("true");
    else if (c=='f') palavra("false");
    else if (c=='n') palavra("null");
    else if (c=='-'||c=='+'||(c>='0'&&c<='9')) numero();
    else erro();
  }

  void lista() {
    if (!come('[')) { erro(); return; }
    espaco();
    if (come(']')) return;
    for (;;) {
      valor(); if (!ok) return;
      espaco();
      if (come(',')) continue;
      if (come(']')) return;
      erro(); return;
    }
  }

  void objeto() {
    if (!come('{')) { erro(); return; }
    espaco();
    if (come('}')) return;
    for (;;) {
      espaco(); texto(); if (!ok) return;
      if (!come(':')) { erro(); return; }
      valor(); if (!ok) return;
      espaco();
      if (come(',')) continue;
      if (come('}')) return;
      erro(); return;                        // lixo depois do valor
    }
  }
};

}  // namespace mini

static bool jsonBemFormado(const std::string& j) {
  mini::P p(j);
  p.espaco();
  p.valor();
  if (!p.ok) return false;
  p.espaco();
  return p.fim();
}

static void teste_T02_aspas_no_json() {
  secao("T02  Mensagem com aspas nao pode quebrar o JSON da interface");
  reiniciarSistema();
  prepararCartao();
  prepararRoboCalibrado();
  prepararProgramaDeSolda();

  // A acao mais comum da maquina: salvar um programa no cartao.
  webPost("/api/sd/salvar?tipo=prog&nome=peca 1");
  esperarCartao();
  rodarComWeb(60);
  nota("mensagem da maquina: %s", ultimaMensagem);
  nota("mensagem do cartao : %s", armMensagem());

  webGet("/api/status");
  const std::string st = webCorpo();
  webGet("/api/sd");
  const std::string sd = webCorpo();

  nota("/api/status -> %s", st.substr(st.size() > 120 ? st.size() - 120 : 0).c_str());
  checar(jsonBemFormado(st), "T02a",
         "/api/status continua sendo JSON valido depois de salvar um arquivo");
  nota("/api/sd     -> %s", sd.c_str());
  checar(jsonBemFormado(sd), "T02b",
         "/api/sd tambem -- e e ele que traz o resultado da gravacao");

  // Agora um nome com barra invertida, que tambem precisa de escape. Ele
  // nao passa por armNomeValido, entao entra pela mensagem direto.
  definirMensagem("teste com \\ barra e \" aspas");
  rodarComWeb(20);
  webGet("/api/status");
  const std::string st2 = webCorpo();
  nota("com barra e aspas: %s", st2.substr(st2.size() > 90 ? st2.size() - 90 : 0).c_str());
  checar(jsonBemFormado(st2), "T02c",
         "barra invertida na mensagem tambem e escapada, nao so a aspa");

  // E o texto tem de CHEGAR: escapar nao pode virar apagar.
  checar(sd.find("peca 1") != std::string::npos, "T02d",
         "e o nome do arquivo continua aparecendo na mensagem: escapar nao e apagar");
}

// =====================================================================
//  T03 - Toda rota JSON, em varios estados, tem de ser JSON valido
// =====================================================================
// O defeito das aspas (T02) so apareceu porque alguem foi olhar. Este
// cenario tira isso do acaso: percorre TODA rota que devolve JSON, em
// estados diferentes da maquina, e passa cada resposta por um analisador
// estrito. Resposta invalida derruba a interface inteira com a maquina
// funcionando -- e o operador ve "sem comunicacao", que manda ele
// procurar defeito no Wi-Fi.
// ---------------------------------------------------------------------
static void teste_T03_todo_json_valido() {
  secao("T03  Toda rota JSON valida, em varios estados da maquina");
  reiniciarSistema();
  prepararCartao();
  prepararRoboCalibrado();

  static const char* JSON_GET[] = {
    // /api/encoder/teste fica de fora: ele devolve text/plain de
    // proposito -- e um relatorio para o operador ler, nao dado.
    "/api/status", "/api/pontos", "/api/trajetoria", "/api/encoder",
    "/api/rede", "/api/sd", "/api/sd/lista?tipo=prog",
    "/api/sd/lista?tipo=traj", "/api/sd/lista?tipo=cfg", "/api/sd/previa",
    "/api/saude", "/api/registro"
  };
  const size_t nR = sizeof(JSON_GET) / sizeof(JSON_GET[0]);

  uint32_t quebradas = 0, conferidas = 0;
  auto varrer = [&](const char* estado) {
    for (size_t i = 0; i < nR; i++) {
      webGet(JSON_GET[i]);
      const std::string corpo = webCorpo();
      conferidas++;
      if (!jsonBemFormado(corpo)) {
        quebradas++;
        nota("QUEBRADA em %s: %s", estado, JSON_GET[i]);
        nota("   %s", corpo.substr(0, 160).c_str());
      }
    }
  };

  varrer("maquina recem-ligada");

  // Estado 2: com programa, trajetoria e um trecho impercorrivel (o
  // aviso do trecho e texto livre indo para dentro do JSON).
  prepararProgramaDeSolda();
  const char* m = nullptr;
  progAdicionarPonto(grausParaPassos(J1, 89.0f), grausParaPassos(J2, 89.0f), &m);
  progDefinirSolda(1, true);
  rodarComWeb(50);
  varrer("com programa e aviso de trecho");

  // Estado 3: mensagem do cartao com aspas, que foi o defeito de T02.
  webPost("/api/sd/salvar?tipo=prog&nome=peca com nome");
  esperarCartao();
  varrer("depois de gravar no cartao");

  webPost("/api/sd/carregar?tipo=prog&nome=nao existe");
  esperarCartao();
  varrer("depois de um erro do cartao");

  // Estado 4: encoder respondendo, e depois mudo.
  prepararEncoder(90, true, 500000);
  rodarComWeb(300);
  varrer("com encoder respondendo");
  g_uart.escravo[0].mudo = true;
  rodarComWeb(600);
  varrer("com o encoder mudo");

  // Estado 5: em falha, que troca mensagens e estados por toda parte.
  g_pinEntrada[PIN_ALARME_J1] = LOW;
  rodarComWeb(300);
  varrer("com alarme de driver");
  g_pinEntrada[PIN_ALARME_J1] = HIGH;
  rodarComWeb(200);

  // Estado 6: previa de peca carregada na area de troca.
  webPost("/api/sd/prever?nome=peca com nome");
  esperarCartao();
  varrer("com previa de peca carregada");

  nota("%lu respostas conferidas, %lu invalidas",
       (unsigned long)conferidas, (unsigned long)quebradas);
  checar(quebradas == 0, "T03a",
         "toda resposta JSON e JSON de verdade, em todo estado exercitado");
}

// =====================================================================
//  T04 - Pausar durante a IDA ao primeiro ponto
// =====================================================================
// A pausa foi pensada para o meio do cordao. Mas ela tambem pega o braco
// na aproximacao -- indo do lugar onde estava ate o ponto 1, antes de
// qualquer arco. Retomar dali tem de RETOMAR A IDA; se o firmware apenas
// declarar "cheguei", ele abre o arco onde o braco parou e puxa a ponta
// ate o inicio do cordao com o arco aberto, riscando a peca no caminho.
// ---------------------------------------------------------------------
static void teste_T04_pausa_na_aproximacao() {
  secao("T04  Pausar na ida ao primeiro ponto, e retomar");
  reiniciarSistema();
  prepararRoboCalibrado();

  // Ponto 1 bem longe de onde o braco esta, para a aproximacao demorar.
  progLimpar();
  const char* m = nullptr;
  progAdicionarPonto(grausParaPassos(J1, 60.0f), grausParaPassos(J2, -60.0f), &m);
  progAdicionarPonto(grausParaPassos(J1, 70.0f), grausParaPassos(J2, -60.0f), &m);
  progDefinirSolda(0, true);
  rodarComWeb(30);

  webPost("/api/prog/executar?ensaio=0&conf=1");
  rodarComWeb(60);
  // Pausa no meio da aproximacao: ainda longe do ponto 1 e sem arco.
  uint32_t t = 0;
  while (fabsf(passosParaGraus(J1, posicaoJ1()) - 20.0f) > 3.0f && t < 8000) {
    rodarComWeb(10); t += 10;
  }
  const float ondePausou = passosParaGraus(J1, posicaoJ1());
  webPost("/api/prog/pausar?on=1");
  rodarComWeb(400);
  nota("pausado a caminho do ponto 1: junta 1 em %.1f graus (o ponto 1 e 60,0), arco=%d, fase=%u",
       (double)ondePausou, (int)soldaLigada(), (unsigned)progFaseTeste());
  checar(progPausado() && !soldaLigada(), "T04a",
         "pausou durante a aproximacao, sem arco");

  webPost("/api/prog/pausar?on=0");
  rodarComWeb(30);

  // No instante em que o arco abrir, o braco TEM de estar no ponto 1.
  float grausAoAbrir = -999.0f;
  t = 0;
  while (t < 20000) {
    rodarComWeb(10); t += 10;
    if (soldaLigada()) { grausAoAbrir = passosParaGraus(J1, posicaoJ1()); break; }
    if (!progRodando()) break;
  }
  nota("o arco abriu com a junta 1 em %.1f graus (o ponto 1 e 60,0)",
       (double)grausAoAbrir);
  checar(fabsf(grausAoAbrir - 60.0f) < 2.0f, "T04b",
         "retomar a aproximacao leva o braco ao ponto 1 ANTES de abrir o arco");

  t = 0;
  while (progRodando() && t < 20000) { rodarComWeb(20); t += 20; }
  nota("fim: rodando=%d, arco=%d, pecas=%lu",
       (int)progRodando(), (int)soldaLigada(),
       (unsigned long)producao.ciclosTotais);
  checar(!progRodando() && !soldaLigada() && producao.ciclosTotais == 1, "T04c",
         "e o programa chega ao fim normalmente");
}

// =====================================================================
//  T05 - Travar no meio de um programa nao pode virar "cheguei"
// =====================================================================
// O vigia de travamento para o EIXO quando o comando anda e o eixo nao --
// continuar dando pulso contra o batente aquece o servo e torce a
// mecanica. Mas parar o eixo nao basta.
//
// As maquinas de estado que rodam por cima (programa, reproducao,
// posicionamento) esperam o movimento acabar para seguir, e "parou" e
// exatamente o sinal delas de "cheguei". Sem tratar o travamento, o
// programa concluia a aproximacao onde o braco tinha travado e ABRIA O
// ARCO ali -- dezenas de graus antes do inicio do cordao, com a ponta
// depois sendo arrastada ate la com o arco aberto.
//
// Travou = a maquina nao esta onde acha que esta. Nao ha percurso
// automatico que se possa continuar dali.
// ---------------------------------------------------------------------
static void teste_T05_travamento_para_o_programa() {
  secao("T05  Travamento no meio de um programa interrompe o programa");
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoder(90, true, 500000);
  rodarComWeb(300);

  // O eixo acompanha o comando de verdade ate travar.
  g_eixoBasePassos = posicaoJ1() - (long)J1.motor->pulsosGerados;
  g_espelharEixo   = true;
  rodarComWeb(200);

  progLimpar();
  const char* m = nullptr;
  progAdicionarPonto(grausParaPassos(J1, 60.0f), grausParaPassos(J2, -20.0f), &m);
  progAdicionarPonto(grausParaPassos(J1, 70.0f), grausParaPassos(J2, -20.0f), &m);
  progDefinirSolda(0, true);
  rodarComWeb(30);

  webPost("/api/prog/executar?ensaio=0&conf=1");
  rodarComWeb(60);
  checar(progRodando(), "T05a", "o programa comecou");

  // A 20 graus o eixo encosta em alguma coisa: o comando continua, o eixo
  // nao anda mais. E o batente, ou o acoplamento que soltou.
  uint32_t t = 0;
  while (passosParaGraus(J1, posicaoJ1()) < 20.0f && t < 8000) {
    rodarComWeb(10); t += 10;
  }
  g_espelharEixo = false;           // o eixo fisico congela onde esta
  g_uart.escravo[0].parar();
  const float ondeTravou = encoderLer(1).graus;
  nota("eixo travado em %.1f graus, comando continua indo para 60",
       (double)ondeTravou);

  // Da tempo de o vigia acusar (ele exige meio segundo).
  t = 0;
  while (progRodando() && t < 6000) { rodarComWeb(20); t += 20; }

  nota("travamentos: %lu | programa rodando: %d | arco: %d | modo: %d",
       (unsigned long)correcaoTravamento().total, (int)progRodando(),
       (int)soldaLigada(), (int)modoAtual);
  nota("mensagem: \"%s\"", ultimaMensagem);
  checar(correcaoTravamento().total > 0, "T05b",
         "o vigia acusou o travamento");
  checar(!progRodando(), "T05c",
         "e o PROGRAMA parou junto: travou nao e cheguei");
  checar(!soldaLigada(), "T05d",
         "o arco nao abriu no lugar onde o braco travou");
  checar(modoAtual == MODO_MANUAL, "T05e",
         "a maquina volta ao manual, para o operador ver o que aconteceu");
}

// =====================================================================
//  U01 - Medir a REDUCAO pelo encoder, contra uma referencia
// =====================================================================
// O encoder esta no eixo do MOTOR, antes do redutor: o angulo que ele
// mostra JA e calculado usando a reducao. Nao da para tirar a reducao
// dele -- seria tirar o numero de uma conta que usa o proprio numero.
//
// O que ele da e a contagem de VOLTAS DO MOTOR, precisa e imune a perda
// de passo. Com UMA referencia do lado da junta (90 graus de esquadro, o
// curso entre batentes, uma volta completa) a reducao sai exata.
//
// A diferenca para a medida antiga, que contava pulsos comandados, e
// justamente essa imunidade -- e este cenario prova as duas coisas.
// ---------------------------------------------------------------------
static void teste_U03_area_da_mesa() {
  secao("U03  A mesa ensinada pelos cantos, e o limite que ela cria");
  reiniciarSistema();
  prepararCartao();
  prepararRoboCalibrado(120.0f);
  // Elos conhecidos, para as contas do cenario serem verificaveis a mao.
  webPost("/api/geometria?l1=200&l2=200&dobra=20&envY=-400&envR=20");
  rodarComWeb(200);
  protEnvelope = true;
  rodarComWeb(50);

  nota("de fabrica: mesa definida=%d, cantos=%u",
       (int)areaMesa.definida, (unsigned)areaMesa.cantos);
  checar(!areaMesa.definida, "U03a",
         "maquina nova nao tem mesa: protecao inventada recusaria movimento valido");

  // Ensina dois cantos opostos levando a ponta ate eles.
  // Espera o comando ser CONSUMIDO antes de esperar o modo voltar: logo
  // apos enviarComando() o modo ainda e MANUAL, e um "espere sair do
  // manual" que comeca no manual termina antes de comecar.
  auto irEEsperar = [&](float a, float b) {
    enviarComando(CMD_MOVER_ANGULOS, 0, 0, a, b);
    rodarComWeb(60);
    uint32_t tt = 0;
    while (modoAtual != MODO_MANUAL && tt < 15000) { rodarComWeb(20); tt += 20; }
  };

  irEEsperar(0.0f, 0.0f);            // ponta em (400, 0)
  webPost("/api/mesa/canto");
  rodarComWeb(200);
  nota("canto 1: %u canto(s), definida=%d",
       (unsigned)areaMesa.cantos, (int)areaMesa.definida);
  checar(areaMesa.cantos == 1 && !areaMesa.definida, "U03b",
         "um canto so nao faz retangulo");

  irEEsperar(60.0f, -60.0f);
  webPost("/api/mesa/canto");
  rodarComWeb(200);
  nota("canto 2: mesa X de %.0f a %.0f, Y de %.0f a %.0f -- \"%s\"",
       (double)areaMesa.xMin, (double)areaMesa.xMax,
       (double)areaMesa.yMin, (double)areaMesa.yMax, ultimaMensagem);
  checar(areaMesa.definida, "U03c",
         "dois cantos afastados definem a area util");

  // Ponto DENTRO da area continua passando.
  const char* m = nullptr;
  const bool dentro = posturaValida(20.0f, -20.0f, &m);
  nota("postura dentro da area: %s", dentro ? "aceita" : m);
  checar(dentro, "U03d", "dentro da mesa o movimento continua valendo");

  // Ponto FORA DA MESA mas DENTRO do curso: e o unico jeito de provar que
  // quem recusou foi a mesa, e nao o limite da junta.
  const float foraT1 = -60.0f;
  const bool fora = posturaValida(foraT1, 0.0f, &m);
  nota("postura fora da area: %s", fora ? "aceita (!)" : m);
  checar(!fora && m && strstr(m, "mesa") != nullptr, "U03e",
         "fora da mesa o movimento e recusado, dizendo qual borda foi passada");

  // E o mais importante: a ponta parada FORA nao pode ficar presa la. A
  // gravidade da violacao tem de apontar o caminho de volta.
  const float gFora = gravidadeViolacao(foraT1, 0.0f);
  const float gMais = gravidadeViolacao(foraT1 + 10.0f, 0.0f);
  const float gMenos= gravidadeViolacao(foraT1 - 10.0f, 0.0f);
  // Nao importa PARA QUE LADO: importa que exista um lado que melhora.
  // E o criterio do jog de recuperacao ("nao piorar"), e e ele que
  // impede o braco de se prender do lado de fora.
  const float gMelhor = (gMais < gMenos) ? gMais : gMenos;
  nota("gravidade fora: %.3f; dez graus para cada lado: %.3f e %.3f",
       (double)gFora, (double)gMais, (double)gMenos);
  checar(gFora > 0.0f && gMelhor < gFora, "U03f",
         "ha sempre um lado que melhora: o braco nunca se prende para fora "
         "da propria mesa");

  // Sobrevive ao religamento e vai junto no backup do cartao.
  const float x0 = areaMesa.xMin, x1 = areaMesa.xMax;
  reiniciarSistemaMantendoNvs();
  nota("depois de religar: definida=%d, X de %.0f a %.0f",
       (int)areaMesa.definida, (double)areaMesa.xMin, (double)areaMesa.xMax);
  checar(areaMesa.definida && fabsf(areaMesa.xMin - x0) < 0.5f &&
         fabsf(areaMesa.xMax - x1) < 0.5f, "U03g",
         "a mesa ensinada sobrevive a queda de energia");

  prepararCartao();
  enviarComandoNomeado(CMD_ARQ_SALVAR_CONFIG, "maquina");
  esperarCartao();
  mesaLimpar();
  rodarComWeb(50);
  checar(!areaMesa.definida, "U03h", "mesa apagada");
  armSolicitar(TAR_CARREGAR_CONFIG, "maquina");
  esperarCartao();
  rodarComWeb(60);
  nota("restaurado do cartao: definida=%d, X de %.0f a %.0f",
       (int)areaMesa.definida, (double)areaMesa.xMin, (double)areaMesa.xMax);
  checar(areaMesa.definida && fabsf(areaMesa.xMin - x0) < 0.5f, "U03i",
         "e o backup do cartao a traz de volta junto com a calibracao");
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
  nota("modo=%d etapa=%d (CAL_J1_POS=%d)", (int)modoAtual, (int)estadoCalib,
       (int)CAL_J1_POS);
  checar(modoAtual == MODO_CALIBRANDO && estadoCalib == CAL_J1_POS, "K02a",
         "a calibracao abre pedindo o limite positivo da junta 1");

  // E exatamente aqui que o operador aperta a seta e ve o braco ir para o
  // outro lado. Mandar cancelar a calibracao para consertar era pedir
  // para ele desistir.
  const int cod = webPost("/api/sentido?j=1&v=1");
  rodarComWeb(200);
  nota("na primeira etapa: HTTP %d -- \"%s\"", cod, ultimaMensagem);
  checar(cod == 200 && J1.inverterDir, "K02b",
         "na primeira etapa da para inverter sem cancelar a calibracao");

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
  printf("\n\033[1mBANCO DE TESTES - Robo2dof v6\033[0m\n");
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

  teste_E01_resolucao_declarada();
  teste_E02_o_zero_e_o_meio_do_curso();
  teste_E03_sem_informar_nada();

  teste_F01_jog_livre_sem_calibracao();
  teste_F02_apagar_calibracao();
  teste_F03_sentido_do_eixo();

  teste_F04_restaurar_padroes_e_apagar_tudo();
  teste_G01_velocidade_igual_entre_juntas();

  teste_H01_velocidade_de_cordao();
  teste_H02_suavidade_da_partida();
  teste_H03_zerar_na_posicao();
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
  teste_L08_uma_pergunta_dois_registradores();
  teste_L09_o_de_sobe_de_verdade();
  teste_L10_configuracao_velha_no_nvs();
  teste_L11_autoteste_dentro_do_sistema();
  teste_L12_cacar_o_registrador();
  teste_L13_velocidade_sentido_e_passos();

  teste_M01_assentar_no_fim_do_movimento();
  teste_M02_nao_retoca_quando_nao_deve();
  teste_M03_desligado_e_parada();
  teste_M04_travamento_nao_dispara_a_toa();
  teste_M05_seguir_o_eixo_solto();
  teste_N01_ensinar_e_recuperar();
  teste_N02_ir_ao_zero_ao_ligar();
  teste_N03_o_que_impede_de_ir();
  teste_P01_ensinar_com_a_mao();
  teste_P02_um_toque_e_um_ponto();
  teste_P03_toque_fora_do_modo();
  teste_P04_sem_encoder_nao_solta_o_braco();
  teste_P05_o_que_encerra_o_aprendizado();
  teste_P06_pela_tela_tambem();
  teste_V01_habilita_pelo_barramento();
  teste_V02_escrita_que_nao_confirma();
  teste_V03_barramento_mudo_e_sem_registrador();
  teste_V04_driver_que_so_aceita_funcao_16();
  teste_V05_registrador_nao_muda_com_torque();
  teste_V06_habilita_nao_prende_o_barramento();
  teste_V07_um_driver_no_barramento();
  teste_V08_zero_sem_calibracao();
  teste_V09_desabilitar_junta_ausente();
  teste_V10_habilitar_so_a_junta_2();
  teste_V11_posicionar_respeita_precisao();
  teste_V12_leitura_absurda_nao_e_confiavel();
  teste_V14_velocidade_por_motor();
  teste_V15_ir_a_um_angulo_sem_calibracao();
  teste_V16_contagem_perdida_e_reancorada();
  teste_V17_junta_com_torque_nao_e_seguida();
  teste_V18_vigia_usa_a_escala_medida();
  teste_V19_calibrar_com_a_mao();
  teste_V20_junta_muda_nao_rouba_o_barramento();
  teste_P07_estop_a_prova_de_falha();
  teste_Q01_pausar_no_meio_do_cordao();
  teste_Q02_contagem_de_pecas();
  teste_Q03_desfazer();
  teste_Q04_arco_exige_confirmacao();
  teste_Q05_backup_leva_a_calibracao();
  teste_Q06_backup_antigo_nao_apaga_calibracao();
  teste_R01_o_segundo_driver();
  teste_S01_rotas_com_lixo();
  teste_S02_nomes_de_arquivo();
  teste_T01_leitura_absurda();
  teste_T02_aspas_no_json();
  teste_T03_todo_json_valido();
  teste_T04_pausa_na_aproximacao();
  teste_T05_travamento_para_o_programa();
  teste_U03_area_da_mesa();
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
