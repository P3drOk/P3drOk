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

// A ENGRENAGEM DE VERDADE DO DRIVER, quando ela nao e a configurada.
//
// `passosPorVolta` e um parametro do DRIVE: alguem troca o drive ou refaz
// uma configuracao e ele muda, sem nada na tela denunciar. O firmware
// continua acreditando no numero digitado, e o braco anda mais (ou menos)
// do que a tela diz.
//
// Zero aqui quer dizer "o driver e o que o firmware pensa que e", que e o
// caso de quase todo cenario. So quem estuda a discordancia entre as duas
// reguas mexe nisto.
static uint32_t g_ppvReal[2] = {0, 0};

static uint32_t ppvFisico(uint8_t k, const Junta& j) {
  return g_ppvReal[k - 1] ? g_ppvReal[k - 1] : j.passosPorVolta;
}

static void espelharUmEixo(uint8_t k, const Junta& j, long base, long perda) {
  if (!j.motor) return;
  if (k == 2 && configEncoder.reg[1] == 0) return;
  const long fisico = base + (long)j.motor->pulsosGerados - perda;
  const float cv  = configEncoder.contagensPorVolta[k - 1];
  // O ENCODER ESTA NO MOTOR, ANTES DO REDUTOR. Entao pulso vira contagem
  // sem a reducao entrar na conta: passos / passos-por-volta = voltas do
  // motor, e voltas x contagens-por-volta = contagem. So dois numeros, os
  // dois do mesmo lado do redutor.
  //
  // A forma antiga passava por passosPorGrau x reducao, que da no mesmo
  // ENQUANTO passosPorGrau = passosPorVolta x reducao / 360. Como e
  // justamente essa igualdade que os cenarios de regua errada quebram, o
  // espelho nao podia depender dela: ele descreve o FERRO, nao a conta que
  // o firmware faz do ferro.
  const uint32_t ppv = ppvFisico(k, j);
  const float voltasMotor = ppv ? ((float)fisico / (float)ppv) : 0.0f;
  g_uart.escravo[k - 1].parar();
  g_uart.escravo[k - 1].posicao = encoderLer(k).referencia
                                + (int32_t)lroundf(voltasMotor * cv);
}

// Encoder colado na CONTAGEM, sem passar pelo espelho.
//
// O espelho soma `pulsosGerados`, que cresce tambem quando o eixo volta:
// ele descreve um movimento so, num sentido so. Um cenario que vai e
// volta -- e a calibracao nova e feita de idas e voltas ao zero --
// precisa de uma leitura que acompanhe a POSICAO, que e o que o encoder
// de verdade faz.
static void colarEncoderNaContagem() {
  for (uint8_t k = 1; k <= 2; k++) {
    Junta& j = (k == 1) ? J1 : J2;
    if (!j.motor) continue;
    // SEM TORQUE o eixo nao segue a contagem: quem manda nele e a mao do
    // operador. Colar tambem ali fecharia um laco -- a contagem escreve
    // no encoder, o encoder escreve na contagem -- e o par sairia
    // correndo junto. Na maquina isso nao existe: o encoder le o ferro.
    if (!j.habilitado) continue;
    if (k == 2 && configEncoder.reg[1] == 0) continue;
    const float cv  = configEncoder.contagensPorVolta[k - 1];
    const float red = (j.reducao > 0.001f) ? j.reducao : 1.0f;
    if (cv < 1.0f) continue;
    const float g = passosParaGraus(j, (k == 1) ? posicaoJ1() : posicaoJ2());
    g_uart.escravo[k - 1].parar();
    g_uart.escravo[k - 1].posicao =
        encoderLer(k).referencia +
        (int32_t)lroundf((g - j.grausHome) * red / 360.0f * cv);
  }
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
  // As protecoes nascem DESLIGADAS na maquina (o braco anda livre e o
  // operador liga o limite quando quiser). Este preparador entrega a
  // maquina "calibrada e protegida", que e o estado em que a maioria dos
  // cenarios quer testar as recusas -- entao ele liga as duas.
  protCurso = true;
  protDobra = true;
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
  // A maquina leva o braco ao zero sozinha e depois solta os motores.
  if (!ateEtapa(CAL_LADO_A)) return false;

  // O operador empurra os DOIS eixos ate um extremo, de uma vez.
  J1.motor->setCurrentPosition(passosPos);
  J2.motor->setCurrentPosition(passosPos);
  enviarComando(CMD_CALIB_CONFIRMAR);
  // Ela energiza, volta os dois ao zero e solta de novo.
  if (!ateEtapa(CAL_LADO_B)) return false;

  J1.motor->setCurrentPosition(passosNeg);
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
  g_ppvReal[0] = g_ppvReal[1] = 0;
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
  nota("Os ramos de supervisionar() (emergencia, conexao) e a");
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
  nota("progIniciar() valida cada trecho pelo que ele realmente percorre --");
  nota("hoje uma RETA no espaco, com solda ou sem. Validar so as pontas");
  nota("deixava o braco raspar a mesa no meio do caminho.");
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
    "\"solda\":%s,\"servos\":%s,\"movendo\":%s,"
    "\"cal1\":%s,\"cal2\":%s,"
    "\"j1min\":%.1f,\"j1max\":%.1f,\"j2min\":%.1f,\"j2max\":%.1f,"
    "\"trajN\":%u,\"trajMs\":%lu,\"trajPct\":%u,\"escala\":%u,"
    "\"progN\":%u,\"progIdx\":%u,\"progPct\":%u,\"ensaio\":%s,\"velCordao\":%.1f,"
    "\"velC\":%.1f,\"protCurso\":%s,\"protDobra\":%s,\"protEnv\":%s,"
    "\"velN\":%lu,\"velA\":%lu,\"velMn\":%lu,\"velMx\":%lu,"
    "\"acel1\":%lu,\"acel2\":%lu,"
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
    "REPRODUZINDO", "J1_NEG", 2u,
    -2000000L, -2000000L, -359.99f, -359.99f, -1999.9f, -1999.9f,
    "false","false","false","false","false",
    -359.9f, 359.9f, -359.9f, 359.9f,
    1500u, 4294967295UL, 100u, 200u,
    40u, 40u, 100u, "false", 999.9f,
    999.9f, "false","false","false",
    180000UL, 180000UL, 180000UL, 180000UL, 999999UL, 999999UL,
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
  const float minAntes = J1.grausMin, maxAntes = J1.grausMax;
  nota("calibracao valida: J1 de %.1f a %.1f graus; braco em %.1f graus",
       J1.grausMin, J1.grausMax, antes);

  // O operador comeca uma nova calibracao -- que ja apaga a marca de
  // calibrada e leva o braco ao zero -- e desiste no meio.
  enviarComando(CMD_CALIB_INICIAR);
  uint32_t t = 0;
  while (estadoCalib != CAL_LADO_A && t < 20000) { rodarComWeb(20); t += 20; }
  nota("no meio da calibracao: calibrada=%d, etapa=%d",
       (int)J1.calibrada, (int)estadoCalib);

  enviarComando(CMD_CALIB_CANCELAR); rodarComWeb(60);

  // Cancelar recupera do NVS os limites que valiam antes. A CONTAGEM nao
  // e mexida: esta calibracao nunca desloca o zero, entao nao ha origem
  // a desfazer -- o braco continua descrevendo onde ele fisicamente esta.
  checar(J1.calibrada &&
         fabsf(J1.grausMin - minAntes) < 0.05f &&
         fabsf(J1.grausMax - maxAntes) < 0.05f, "A11",
         "cancelar devolve os limites que valiam antes");
  nota("apos cancelar: calibrada=%d, limites %.1f a %.1f",
       (int)J1.calibrada, J1.grausMin, J1.grausMax);
  nota("O zero nao se move nesta calibracao, entao nao ha origem a");
  nota("desfazer: os limites do NVS voltam a se referir ao mesmo ponto");
  nota("de onde sairam.");
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
// E02: o ZERO fica onde esta.
//
// Antes o operador declarava, num campo, o angulo real do braco na
// posicao de referencia. Depois disso houve uma versao em que o zero
// virava o MEIO do curso medido.
//
// Nenhuma das duas: o zero e o ponto ao qual o operador voltou duas vezes
// durante a calibracao e viu o braco parar. Ele conhece esse ponto -- e o
// unico da maquina que ele conhece. Desloca-lo no fim seria troca-lo
// debaixo dele. Os limites saem assimetricos quando a maquina e
// assimetrica, que e o honesto.
// ---------------------------------------------------------------------
static void teste_E02_o_zero_fica_onde_esta() {
  secao("E02  O zero e o ponto ao qual o operador voltou, e nao se move");
  reiniciarSistema();
  enviarComando(CMD_SERVOS, 1); rodarComWeb(30);

  // Marcas assimetricas de proposito: o operador nao para no meio, ele
  // para nos batentes, e eles raramente sao simetricos.
  const float ppg = J1.passosPorGrau;
  const long pos = (long)(+100.0f * ppg);
  const long neg = (long)( -20.0f * ppg);
  rodarAssistente(neg, pos);

  nota("marcas em %ld e %ld pulsos -> curso J1 de %.1f a %.1f graus",
       neg, pos, J1.grausMin, J1.grausMax);
  checar(fabsf(J1.grausMin + 20.0f) < 0.6f &&
         fabsf(J1.grausMax - 100.0f) < 0.6f, "E02a",
         "os limites saem onde os batentes estao, contados do zero -- "
         "assimetricos, porque a maquina e assimetrica");

  // E o braco termina no zero: a ultima viagem da calibracao o traz de
  // volta e deixa com torque.
  const float ondeEsta = passosParaGraus(J1, posicaoJ1());
  nota("o braco terminou em %.2f graus", ondeEsta);
  checar(fabsf(ondeEsta) < 1.0f, "E02b",
         "e a calibracao termina com o braco no zero, com torque: entregar "
         "a maquina largada no batente seria devolve-la pior");

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

  // E depois de calibrado as protecoes valem -- QUANDO LIGADAS.
  //
  // Elas nascem desligadas: o braco anda livre pela mesa, e limite e
  // coisa que o operador liga depois de conferir que os numeros
  // descrevem a maquina dele. Calibrar mede o curso; nao decide sozinho
  // que ele vai ser imposto.
  const bool livreDepoisDeCalibrar = posturaValida(0.0f, 170.0f, nullptr);
  checar(livreDepoisDeCalibrar, "F01c1",
         "calibrar mede o curso, nao liga a protecao sozinho: o braco "
         "continua livre ate o operador pedir limite");
  protCurso = true;
  protDobra = true;
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
  const int cod = webPost("/api/config?velN=20&velA=12&velCordao=7.5"
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

// ---------------------------------------------------------------------
// I04: o DESLOCAMENTO tambem anda em linha reta.
//
// O trecho sem solda era interpolado nas juntas -- mais rapido, e a ponta
// fazia uma curva. Num programa desenhado de cima isso e inofensivo. Num
// caminho ENSINADO COM A MAO, nao: quem levou o braco ponto a ponto quase
// sempre desviou de alguma coisa, e a curva passa por fora do que ele
// mostrou. O braco tem de ir por onde foi ensinado, com arco ou sem.
//
// A diferenca entre os dois caminhos e grande e facil de medir: a corda e
// o arco de uma junta que gira dezenas de graus ficam a dezenas de
// milimetros um do outro.
// ---------------------------------------------------------------------
static void teste_I04_deslocamento_tambem_e_reta() {
  secao("I04  O deslocamento sem solda tambem segue a reta");
  reiniciarSistema();
  prepararRoboCalibrado(170.0f);
  webPost("/api/geometria?l1=400&l2=300");
  rodarComWeb(120);

  // Duas posturas cuja interpolacao nas juntas descreve uma curva bem
  // aberta: a junta 1 gira muito e a 2 quase nada.
  const float A1 = -40.0f, A2 = 60.0f;
  const float B1 =  40.0f, B2 = 60.0f;
  PontoXY v[2];
  float xc, yc;
  cinematicaDireta(A1, A2, xc, yc, v[0].x, v[0].y);
  cinematicaDireta(B1, B2, xc, yc, v[1].x, v[1].y);
  nota("A=(%.0f, %.0f) mm  B=(%.0f, %.0f) mm",
       (double)v[0].x, (double)v[0].y, (double)v[1].x, (double)v[1].y);

  // Quanto a interpolacao NAS JUNTAS sairia da reta: o meio do arco.
  float mx, my;
  cinematicaDireta((A1 + B1) / 2.0f, (A2 + B2) / 2.0f, xc, yc, mx, my);
  const float desvioDoArco = distSegmento(mx, my, v[0].x, v[0].y, v[1].x, v[1].y);
  nota("o meio do arco das juntas fica a %.0f mm da reta A-B",
       (double)desvioDoArco);
  checar(desvioDoArco > 20.0f, "I04a",
         "os dois caminhos sao mesmo diferentes aqui -- senao o cenario "
         "passaria a verde sem distinguir coisa nenhuma");

  progLimpar();
  const char* m = nullptr;
  progAdicionarPonto(grausParaPassos(J1, A1), grausParaPassos(J2, A2), &m);
  progAdicionarPonto(grausParaPassos(J1, B1), grausParaPassos(J2, B2), &m);
  progDefinirSolda(0, false);          // DESLOCAMENTO, sem arco

  J1.motor->setCurrentPosition(grausParaPassos(J1, A1));
  J2.motor->setCurrentPosition(grausParaPassos(J2, A2));
  rodarComWeb(20);

  enviarComando(CMD_PROG_EXECUTAR, 1);   // ensaio
  rodarComWeb(60);
  checar(progRodando(), "I04b", "o programa de deslocamento parte");

  float pior = 0.0f;
  uint32_t t = 0;
  while (progRodando() && t < 60000) {
    if (t % 200 == 0) registrarContatoOperador();
    rodar(1); t++;
    float x, y; pontaAgora(x, y);
    const float d = distPolilinha(x, y, v, 2);
    if (d > pior) pior = d;
  }
  nota("desvio maximo da reta durante o deslocamento: %.1f mm "
       "(o arco das juntas passaria a %.0f mm)", (double)pior,
       (double)desvioDoArco);
  // A folga e maior que a do cordao (I01 cobra 2 mm) porque o deslocamento
  // anda mais rapido e o seguidor atrasa mais -- e nao ha arco aberto para
  // marcar a peca. O que este cenario separa nao e 3 mm de 5: e RETA de
  // ARCO, e entre os dois ha duas ordens de grandeza.
  checar(pior < desvioDoArco / 10.0f, "I04c",
         "a ponta segue a RETA no trecho sem solda, em vez de descrever o "
         "arco da interpolacao nas juntas");
  if (progRodando()) progParar();
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

// Manda o braco para um angulo e espera o movimento E o assentamento
// terminarem, com paciencia dada pelo cenario. Quem tem um erro grande
// para fechar em passos de tres graus precisa de mais que os oito segundos
// do irComPerda.
static void irEsperando(float t1, float t2, uint32_t limiteMs) {
  webPost((std::string("/api/mover?t1=") + std::to_string((int)t1) +
           "&t2=" + std::to_string((int)t2)).c_str());
  rodarComWeb(60);
  uint32_t t = 0;
  while (modoAtual != MODO_MANUAL && t < limiteMs) { rodarComWeb(20); t += 20; }
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

  // 2. Erro GRANDE: fecha, mas EM PASSOS.
  //
  // Antes, erro acima de maxCorrecaoGraus era recusado e o braco ficava
  // onde estava -- era isso que fazia "peco zero grau e quem chega e so
  // o tracejado". O teto virou o tamanho do PASSO: cada retoque anda no
  // maximo maxCorrecaoGraus, le o encoder de novo e repete. A intencao
  // da regra continua de pe -- o braco nunca lunga varios graus de uma
  // vez -- e o erro fecha assim mesmo.
  const float teto = configCorrecao.maxCorrecaoGraus;
  irComPerda(25, 5, 9.0f);
  nota("perda de 9 graus (teto de %.1f por retoque): eixo em %.3f, "
       "%u retoque(s), estado %u -- \"%s\"",
       (double)teto, (double)eixoFisicoGraus(),
       (unsigned)correcaoResumo().tentativas,
       (unsigned)correcaoResumo().estado, correcaoResumo().motivo);
  checar(fabsf(eixoFisicoGraus() - 25.0f) < 0.15f, "M02b",
         "erro grande fecha assim mesmo: o BRACO chega ao alvo");
  checar(correcaoResumo().tentativas >= 3, "M02c",
         "e fecha em varios retoques, nenhum maior que o teto -- "
         "o braco nunca lunga nove graus de uma vez");

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

// ---------------------------------------------------------------------
// M06: o braco chega ao angulo mesmo na maquina do relato -- junta SEM
//      curso medido, limite desligado, e erro maior que o teto.
//
// Relato: "peco para ir ao angulo tal e ele nao chega; quem chega e
// apenas o tracejado em vermelho. Deve se basear no encoder, nao no
// erro".
//
// Eram TRES portoes fechados ao mesmo tempo, cada um bastando sozinho
// para o braco ficar onde estava:
//
//   1. faltaPara() exigia curso medido -- junta sem calibracao nao
//      recebia assentamento nenhum;
//   2. erro acima de maxCorrecaoGraus era RECUSADO em vez de fechado em
//      passos;
//   3. o retoque era preso ao curso calibrado mesmo com o limite
//      desligado -- e num curso medido pela metade ele nao cabia.
// ---------------------------------------------------------------------
static void teste_M06_chega_sem_curso_e_com_erro_grande() {
  secao("M06  Sem curso medido, limite desligado e erro grande: chega");
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoder(90, true, 0);
  rodarComWeb(300);
  enviarComando(CMD_ENCODER_ZERAR, 0);
  rodarComWeb(120);
  g_espelharEixo = true;

  // A maquina do relato: junta 1 sem curso medido e limite desligado --
  // o braco anda livre pela mesa, que e o padrao da maquina.
  J1.calibrada = false;
  protCurso = false;
  rodarComWeb(50);

  const float teto = configCorrecao.maxCorrecaoGraus;
  nota("junta 1 calibrada=%d, protCurso=%d, teto de retoque %.1f grau",
       (int)J1.calibrada, (int)protCurso, (double)teto);

  irComPerda(30, 0, 7.0f);
  nota("pedi 30 graus com 7 de perda: eixo em %.3f, %u retoque(s), "
       "estado %u -- \"%s\"",
       (double)eixoFisicoGraus(), (unsigned)correcaoResumo().tentativas,
       (unsigned)correcaoResumo().estado, correcaoResumo().motivo);
  checar(fabsf(eixoFisicoGraus() - 30.0f) < 0.15f, "M06a",
         "o BRACO chega ao angulo pedido, sem curso medido e com o erro "
         "acima do teto -- antes ele parava no tanto do erro");
  checar(correcaoResumo().estado == CORR_PRONTA, "M06b",
         "e o assentamento fecha, em vez de recusar");

  // Sai e volta: o lugar tem de ser o mesmo, que e o pedido de sempre.
  irComPerda(0, 0, 2.0f);
  irComPerda(30, 0, 2.0f);
  nota("saiu e voltou: eixo em %.3f grau", (double)eixoFisicoGraus());
  checar(fabsf(eixoFisicoGraus() - 30.0f) < 0.15f, "M06c",
         "sai, volta e cai no mesmo lugar");

  // E o caso exato da maquina do relato: a junta ESTA "calibrada", mas
  // com um curso ridiculo de quatro graus -- uma calibracao que parou no
  // meio -- e o braco esta bem longe dali. Com o limite desligado, esse
  // curso nao pode prender o retoque.
  const long dois = (long)(2.0f * J1.passosPorGrau);
  J1.calibrada = true;
  J1.passosMin = -dois; J1.passosMax = dois;
  recalcularResolucao();
  protCurso = false;
  rodarComWeb(50);
  nota("curso medido pela metade: %.1f a %.1f graus, braco indo a 30",
       (double)J1.grausMin, (double)J1.grausMax);

  irComPerda(30, 0, 5.0f);
  nota("com curso de quatro graus e limite desligado: eixo em %.3f, "
       "estado %u -- \"%s\"", (double)eixoFisicoGraus(),
       (unsigned)correcaoResumo().estado, correcaoResumo().motivo);
  checar(fabsf(eixoFisicoGraus() - 30.0f) < 0.15f, "M06d",
         "curso medido pela metade nao prende o retoque quando o limite "
         "esta desligado -- era mais um jeito de o braco nao chegar");

  // Com o limite LIGADO o curso volta a valer, inclusive para o retoque:
  // ali ele e uma excecao que nunca se abre.
  protCurso = true;
  rodarComWeb(50);
  const float ondeEstava = eixoFisicoGraus();
  irComPerda(30, 0, 5.0f);
  nota("limite ligado: eixo saiu de %.2f para %.2f -- \"%s\"",
       (double)ondeEstava, (double)eixoFisicoGraus(), correcaoResumo().motivo);
  checar(eixoFisicoGraus() < 30.0f - 1.0f, "M06e",
         "e com o limite LIGADO o retoque respeita o curso, como sempre");
  protCurso = false;
}

// ---------------------------------------------------------------------
// M07: soltar o numero de tentativas nao pode virar licenca para
//      martelar o eixo.
//
// O assentamento passou a insistir enquanto APROXIMA, em vez de parar
// num numero fixo de tentativas. A pergunta que sobra e: e quando o eixo
// simplesmente nao segue -- acoplamento solto, driver desarmado?
//
// A resposta e que o assentamento nem chega a rodar. O vigia de
// travamento pega antes: comando andando e medido parado e a definicao
// de travamento, e ele para o eixo e diz. Este cenario prende essa
// ordem, que e o que impede o retoque de bater no ferro.
// ---------------------------------------------------------------------
static void teste_M07_desiste_quando_nao_aproxima() {
  secao("M07  Eixo que nao segue e pego antes de o retoque martelar");
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoder(90, true, 0);
  rodarComWeb(300);
  enviarComando(CMD_ENCODER_ZERAR, 0);
  rodarComWeb(120);

  // O eixo NAO segue os pulsos: o encoder fica onde esta, faca o retoque
  // o que fizer. E o que se ve com o acoplamento solto.
  g_espelharEixo = false;
  irComPerda(20, 0, 0.0f);

  nota("encoder preso: %u retoque(s), estado %u -- \"%s\"; travamento=%d "
       "total=%lu; modo=%d; msg=\"%s\"",
       (unsigned)correcaoResumo().tentativas,
       (unsigned)correcaoResumo().estado, correcaoResumo().motivo,
       (int)correcaoTravamento().ativo, (unsigned long)correcaoTravamento().total,
       (int)modoAtual, ultimaMensagem);
  checar(correcaoTravamento().total > 0, "M07a",
         "o eixo que nao segue e pego pelo vigia de travamento, e a "
         "maquina diz qual junta e o que pode ter acontecido");
  checar(correcaoResumo().tentativas <= 8, "M07b",
         "e o retoque nao martela o ferro: pouquissimas tentativas, muito "
         "abaixo do teto absoluto de 40");
  checar(modoAtual == MODO_MANUAL, "M07c",
         "e o robo nao fica preso em POSICIONANDO");
}

// ---------------------------------------------------------------------
// M08: REGUA DISCORDANTE -- a maquina do relato.
//
// "Peco para ir a tal angulo: comeca bem, no meio do caminho da uns
// travamentos e nunca chega; e quando o caminho e curto ele passa do
// ponto e nao tem ajuste que o traga de volta."
//
// Os dois sintomas sao a mesma condicao: passosPorGrau (catalogo:
// pulsos por volta x reducao) discordando de contagensPorGrau (medido).
// Aqui o eixo entrega DOIS graus de encoder para cada grau comandado.
//
//   1. o vigia de travamento dividia pelo catalogo e comparava com o
//      medido: o esperado saia varias vezes maior que o real e um eixo
//      andando perfeitamente virava "travado" meio segundo depois de
//      arrancar;
//   2. o retoque pedia o erro cru em graus, andava o dobro, passava do
//      ponto, voltava passando de novo -- e o assentamento desistia
//      dizendo que nao aproximava.
// ---------------------------------------------------------------------
static void teste_M08_regua_discordante_chega_mesmo_assim() {
  secao("M08  Regua discordante: sem travamento falso, e chega ao ponto");
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoder(90, true, 0);
  rodarComWeb(300);
  enviarComando(CMD_ENCODER_ZERAR, 0);
  rodarComWeb(120);
  g_espelharEixo = true;

  // A regua MEDIDA diz metade das contagens por grau que a fisica do
  // espelho entrega. Efeito: cada grau comandado vira dois graus lidos.
  const float red    = (J1.reducao > 0.001f) ? J1.reducao : 1.0f;
  const float cpgFis = configEncoder.contagensPorVolta[0] * red / 360.0f;
  configEncoder.contagensPorGrau[0] = cpgFis / 2.0f;
  rodarComWeb(200);
  nota("regua fisica %.1f c/grau, regua medida %.1f -- o eixo entrega o "
       "dobro do que se pede", (double)cpgFis,
       (double)configEncoder.contagensPorGrau[0]);

  const uint32_t travAntes = correcaoTravamento().total;
  irComPerda(20, 0, 0.0f);

  nota("pedi 20 graus: encoder le %.2f, %u retoque(s), travamentos %lu, "
       "estado %u -- \"%s\"",
       (double)encoderLer(1).graus, (unsigned)correcaoResumo().tentativas,
       (unsigned long)(correcaoTravamento().total - travAntes),
       (unsigned)correcaoResumo().estado, correcaoResumo().motivo);

  checar(correcaoTravamento().total == travAntes, "M08a",
         "regua discordante NAO vira travamento: eixo que gira produz "
         "contagem, e era isso que travava no meio do caminho");
  checar(fabsf(encoderLer(1).graus - 20.0f) < 0.3f, "M08b",
         "e o braco chega ao angulo pedido, medido pelo encoder");

  // O ganho ficou aprendido: o proximo posicionamento ja nasce certo, e
  // fecha com menos retoque que o primeiro.
  const uint8_t retoquesPrimeiro = correcaoResumo().tentativas;
  irComPerda(35, 0, 0.0f);
  nota("segundo movimento: encoder le %.2f com %u retoque(s) "
       "(o primeiro precisou de %u)",
       (double)encoderLer(1).graus, (unsigned)correcaoResumo().tentativas,
       (unsigned)retoquesPrimeiro);
  checar(fabsf(encoderLer(1).graus - 35.0f) < 0.3f, "M08c",
         "o segundo movimento tambem chega");
  checar(correcaoResumo().tentativas <= retoquesPrimeiro, "M08d",
         "e com o ganho ja medido ele nao precisa de mais retoques que o "
         "primeiro -- a maquina aprendeu a propria escala");

  // CAMINHO CURTO, que era o caso do "passa e nao volta".
  irComPerda(37, 0, 0.0f);
  nota("caminho curto (2 graus): encoder le %.2f -- \"%s\"",
       (double)encoderLer(1).graus, correcaoResumo().motivo);
  checar(fabsf(encoderLer(1).graus - 37.0f) < 0.3f, "M08e",
         "caminho curto tambem cai no ponto, em vez de passar e ficar la");
}

// ---------------------------------------------------------------------
// M09: o vigia de travamento nao pode ser enganado pela propria regua.
//
// A conta proporcional divide por passosPorGrau (catalogo) e compara com
// contagensPorGrau (medido). Quando o medido e varias vezes maior que a
// fisica, o ESPERADO sai varias vezes maior que o real -- e um eixo
// andando perfeitamente e declarado travado meio segundo depois de
// arrancar. Era o "comeca bem e no meio do caminho trava".
//
// Eixo que gira produz contagem, seja qual for a escala. O teste sem
// escala -- pulso correndo, encoder PARADO -- e o unico que nao mente
// por numero errado, e virou condicao necessaria.
// ---------------------------------------------------------------------
static void teste_M09_regua_errada_nao_inventa_travamento() {
  secao("M09  Regua errada nao inventa travamento no meio do caminho");
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoder(90, true, 0);
  rodarComWeb(300);
  enviarComando(CMD_ENCODER_ZERAR, 0);
  rodarComWeb(120);
  g_espelharEixo = true;

  // A regua medida diz SEIS vezes mais contagens por grau do que a
  // fisica entrega: o esperado fica seis vezes acima do real, bem abaixo
  // do quinto que a conta proporcional exige.
  const float red    = (J1.reducao > 0.001f) ? J1.reducao : 1.0f;
  const float cpgFis = configEncoder.contagensPorVolta[0] * red / 360.0f;
  configEncoder.contagensPorGrau[0] = cpgFis * 6.0f;
  rodarComWeb(200);

  const uint32_t travAntes = correcaoTravamento().total;
  const long passosAntes = posicaoJ1();

  webPost("/api/mover?t1=60&t2=0");
  rodarComWeb(60);
  uint32_t t = 0;
  while (modoAtual != MODO_MANUAL && t < 12000) { rodarComWeb(20); t += 20; }

  const float andouGraus =
      fabsf((float)(posicaoJ1() - passosAntes)) / J1.passosPorGrau;
  nota("regua medida 6x a fisica: travamentos %lu, eixo andou %.1f graus "
       "de contagem -- \"%s\"",
       (unsigned long)(correcaoTravamento().total - travAntes),
       (double)andouGraus, ultimaMensagem);

  checar(correcaoTravamento().total == travAntes, "M09a",
         "eixo andando com a regua errada NAO e travamento: ele produz "
         "contagem, e contagem e o que separa preso de solto");
  checar(andouGraus > 30.0f, "M09b",
         "e o movimento vai ate o fim, em vez de morrer no meio");
}

// ---------------------------------------------------------------------
// M10: leitura que PULA nao vira posicao -- mas movimento de verdade sim.
//
// Uma leitura pode ser numericamente possivel e ainda assim nao ser o
// eixo: quadro corrompido que passou no CRC, palavra baixa de um
// instante casada com a alta de outro, contador dando a volta. Todos
// chegam como um inteiro plausivel, e o que os denuncia e a DISTANCIA
// ate a leitura anterior -- meia volta ou uma volta do motor de uma
// amostra para a outra, que nenhum eixo desta maquina faz.
//
// Obedecer teleporta a posicao oficial: o desenho salta, o ancoramento
// reescreve a contagem com o numero errado e o braco arranca para o
// lugar errado. Era o "braco pulando angulo".
//
// E o que separa glitch de movimento grande e de verdade: o glitch NAO
// SE REPETE. Por isso o salto fica pendente e a amostra seguinte decide.
// ---------------------------------------------------------------------
static void teste_M10_salto_impossivel_nao_vira_posicao() {
  secao("M10  Leitura que pula longe demais nao vira posicao");
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoder(90, true, 0);
  rodarComWeb(300);
  enviarComando(CMD_ENCODER_ZERAR, 0);
  rodarComWeb(300);

  const float cv = configEncoder.contagensPorVolta[0];
  const int32_t base = g_uart.escravo[0].posicao;
  const float grausAntes = encoderLer(1).graus;
  const uint32_t saltosAntes = encoderLer(1).saltos;

  // A posicao pula uma volta de motor adiante em UMA amostra so -- essa e
  // a assinatura do quadro corrompido, que aparece e some. Enquanto ela
  // esta la, a maquina NAO pode adotar aquele angulo: e nesse instante
  // que o desenho saltava e o ancoramento reescrevia a contagem errada.
  g_uart.escravo[0].posicao = base + (int32_t)cv;
  float    durante       = grausAntes;
  uint32_t saltosDurante = saltosAntes;
  for (int i = 0; i < 40; i++) {
    rodarComWeb(10);
    saltosDurante = encoderLer(1).saltos;
    if (saltosDurante > saltosAntes) break;
  }
  durante = encoderLer(1).graus;

  // O barramento volta ao normal na amostra seguinte.
  g_uart.escravo[0].posicao = base;
  rodarComWeb(400);

  nota("glitch de uma volta: antes %.2f, DURANTE %.2f, depois %.2f graus; "
       "%lu salto(s) recusado(s)", (double)grausAntes, (double)durante,
       (double)encoderLer(1).graus,
       (unsigned long)(saltosDurante - saltosAntes));
  checar(saltosDurante > saltosAntes, "M10a",
         "o quadro que pulou uma volta inteira e recusado, e contado");
  checar(fabsf(durante - grausAntes) < 1.0f, "M10b",
         "e a posicao da maquina nao se mexe enquanto o glitch esta la -- "
         "era nesse instante que o desenho saltava");

  // MOVIMENTO DE VERDADE para longe: a segunda amostra confirma, e a
  // maquina aceita. Recusar aqui seria pior que o defeito -- o braco
  // movido a mao ficaria invisivel.
  const float longe = grausAntes + 40.0f;
  const float cpg = (configEncoder.contagensPorGrau[0] != 0.0f)
                  ? configEncoder.contagensPorGrau[0]
                  : cv * ((J1.reducao > 0.001f) ? J1.reducao : 1.0f) / 360.0f;
  g_uart.escravo[0].posicao = base + (int32_t)lroundf(40.0f * cpg);
  rodarComWeb(600);
  nota("movimento real de 40 graus: encoder le %.2f", (double)encoderLer(1).graus);
  checar(fabsf(encoderLer(1).graus - longe) < 1.0f, "M10c",
         "movimento grande DE VERDADE passa: a amostra seguinte confirma, "
         "e o glitch e justamente o que nao se repete");
}

// ---------------------------------------------------------------------
// M11: a maquina afere a propria engrenagem no movimento comum.
//
// `passosPorGrau = passosPorVolta x reducao / 360`, e os dois sao
// DIGITADOS. Pulsos por volta e parametro do DRIVER: muda quando alguem
// troca o drive ou refaz uma configuracao, e nada na tela denuncia. Com
// ele errado, todo movimento passa do angulo pedido pelo MESMO fator,
// sempre -- era o "esta passando do ponto de grau enviado".
//
// A conta que conserta ja existia, mas so rodava na viagem ao zero da
// calibracao guiada. Numa maquina que nunca calibrou -- que e o caso do
// relato -- ela nunca rodava, e a regua ficava errada para sempre.
//
// Repare que a REDUCAO nao entra: o encoder esta no eixo do motor, antes
// do redutor, e pulso e contagem estao os dois do mesmo lado dele.
// ---------------------------------------------------------------------
static void teste_M11_maquina_afere_a_propria_engrenagem() {
  secao("M11  A maquina mede a propria engrenagem no movimento comum");
  reiniciarSistema();

  // O DRIVE de verdade quer metade dos pulsos que o firmware acredita.
  // Efeito: cada grau pedido sai com o dobro de pulso, e o braco anda o
  // dobro. Sem calibracao guiada nenhuma -- contagensPorGrau segue zero,
  // como na maquina do operador.
  //
  // A engrenagem se mexe ANTES do preparador: e dela que sai passosPorGrau,
  // e e por passosPorGrau que o curso em graus e calculado. Mexer depois
  // deixaria a maquina com metade do curso que o cenario pensa ter -- e o
  // movimento seria recusado por limite, nao executado com a regua errada,
  // que e o que se quer estudar aqui.
  const uint32_t ppvReal = J1.passosPorVolta;
  g_ppvReal[0] = ppvReal;
  J1.passosPorVolta = ppvReal * 2;
  recalcularResolucao();

  prepararRoboCalibrado(180.0f);
  // Como na maquina do operador: o limite de curso nasce desligado.
  protCurso = false;
  prepararEncoder(90, true, 0);
  rodarComWeb(300);
  enviarComando(CMD_ENCODER_ZERAR, 0);
  rodarComWeb(120);
  g_espelharEixo = true;
  rodarComWeb(200);
  nota("drive quer %lu pulsos/volta, firmware acredita em %lu -- cada grau "
       "pedido anda dois", (unsigned long)ppvReal,
       (unsigned long)J1.passosPorVolta);

  // Espera longa de proposito: o primeiro movimento chega ao DOBRO do
  // angulo, e e o assentamento que traz o braco de volta, em passos de tres
  // graus. Esse resgate demorado acontece uma vez na vida da maquina --
  // depois dele a regua esta certa.
  irEsperando(45, 0, 25000);
  const float erroPrimeiro = fabsf(correcaoResumo().erroInicial1);
  nota("1o movimento (pedi 45): o braco chegou em %.2f com erro inicial de "
       "%.1f grau; engrenagem agora %lu pulsos/volta",
       (double)encoderLer(1).graus, (double)erroPrimeiro,
       (unsigned long)J1.passosPorVolta);

  checar(erroPrimeiro > 10.0f, "M11a",
         "o primeiro movimento REALMENTE passa do ponto -- e ele que carrega "
         "a medida, e sem esse erro nao haveria o que aferir");
  checar(J1.passosPorVolta > ppvReal * 9 / 10 &&
         J1.passosPorVolta < ppvReal * 11 / 10, "M11b",
         "e o proprio movimento mediu a engrenagem do drive, sem calibracao "
         "guiada, sem transferidor e sem saber a reducao");
  checar(fabsf(encoderLer(1).graus - 45.0f) < 0.3f, "M11c",
         "o assentamento ainda leva este primeiro movimento ao ponto");

  // O SEGUNDO movimento e o que o operador vai sentir: com a regua certa,
  // ele nasce no lugar em vez de ser resgatado pelo assentamento.
  irEsperando(20, 0, 25000);
  const float erroSegundo = fabsf(correcaoResumo().erroInicial1);
  nota("2o movimento (pedi 20): chegou em %.2f com erro inicial de %.2f grau "
       "-- contra %.1f do primeiro",
       (double)encoderLer(1).graus, (double)erroSegundo, (double)erroPrimeiro);
  checar(erroSegundo < 1.0f, "M11d",
         "o movimento seguinte ja cai no angulo pedido sozinho: e isto que "
         "separa prever de remediar");
  checar(fabsf(encoderLer(1).graus - 20.0f) < 0.3f, "M11e",
         "e chega, medido pelo encoder");
}

// ---------------------------------------------------------------------
// M12: o assentamento nao pode desistir por ARITMETICA.
//
// O passo do retoque e limitado a maxCorrecaoGraus (3 por padrao), e o
// criterio de progresso exigia que o erro caisse 15% A CADA PASSO. Um
// passo de 3 graus so consegue 15% enquanto o erro for menor que 20:
// acima disso a desistencia estava decidida antes de comecar, e o
// assentamento parava em tres retoques com dezenas de graus na peca,
// dissesse o que dissesse o encoder.
//
// Era metade do "comeca bem, da alguns travamentos e nunca chega ao local
// desejado". A pergunta certa nao e "sobrou pouco?", e "o passo fez o que
// tinha como fazer?".
// ---------------------------------------------------------------------
static void teste_M12_nao_desiste_por_aritmetica() {
  secao("M12  Erro grande fecha em varios passos, em vez de desistir");
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoder(90, true, 0);
  rodarComWeb(300);
  enviarComando(CMD_ENCODER_ZERAR, 0);
  rodarComWeb(120);
  g_espelharEixo = true;

  const float teto = configCorrecao.maxCorrecaoGraus;
  // 40 graus, e nao 25, por um motivo aritmetico exato: com passos de 3 e
  // 15% do erro TOTAL exigidos, um erro de 25 ainda escapa -- na terceira
  // tentativa 16 < 19x0,85 e o contador zera por sorte. Acima de 28 nao ha
  // escapatoria, e a desistencia acontece sempre. Um cenario tem de cair do
  // lado em que o defeito e certo, senao ele passa a verde sem o conserto.
  const float perda = 40.0f;
  nota("teto do passo %.1f grau, %u sem-progresso permitidos: com 15%% do "
       "ERRO INTEIRO exigidos, nada acima de %.0f graus conseguia progredir",
       (double)teto, (unsigned)configCorrecao.tentativas, (double)(teto / 0.15f));

  // Escorregao de 25 graus no meio do caminho: o assentamento tem de
  // fechar isso em passos de tres.
  webPost("/api/mover?t1=45&t2=0");
  rodarComWeb(60);
  perderPassos(perda);
  uint32_t t = 0;
  while (modoAtual != MODO_MANUAL && t < 25000) { rodarComWeb(20); t += 20; }

  const ResumoCorrecao rc = correcaoResumo();
  nota("escorregao de %.0f graus: %u retoque(s), estado %u, encoder le %.2f "
       "-- \"%s\"", (double)perda, (unsigned)rc.tentativas,
       (unsigned)rc.estado, (double)encoderLer(1).graus, rc.motivo);

  checar(rc.tentativas > (uint16_t)(perda / teto / 2.0f), "M12a",
         "o assentamento insiste os passos que o erro exige, em vez de "
         "desistir no terceiro");
  checar(rc.estado != CORR_DESISTIU, "M12b",
         "e nao desiste dizendo que nao aproxima -- ele estava aproximando "
         "o tempo todo, so nao 15% de um erro grande por passo de tres graus");
  checar(fabsf(encoderLer(1).graus - 45.0f) < 0.5f, "M12c",
         "o braco chega ao angulo pedido, medido pelo encoder");
}

// ---------------------------------------------------------------------
// M13: silencio do barramento nao e eixo parado.
//
// Quando uma leitura falha, `velocidade` e zerada de proposito: a tela nao
// pode dizer que o eixo gira depois que o fio caiu. So que o vigia de
// travamento le esse mesmo campo, e para ele um zero e "o eixo nao esta
// respondendo ao pulso".
//
// A leitura so deixa de ser confiavel depois de UM SEGUNDO inteiro, e o
// vigia dispara com MEIO. Sobrava uma janela em que uma rajada de falhas
// enchia o criterio sozinha -- e o braco parava com "Junta travada" por
// causa do barramento, no meio de um cordao. No barramento do relato (4,5
// leituras/s, 7% de falha) bastavam tres respostas perdidas seguidas.
// ---------------------------------------------------------------------
static void teste_M13_silencio_nao_e_travamento() {
  secao("M13  Rajada de falhas no barramento nao vira travamento");
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoder(90, true, 0);
  rodarComWeb(300);
  enviarComando(CMD_ENCODER_ZERAR, 0);
  rodarComWeb(120);
  g_espelharEixo = true;

  const uint32_t travAntes  = correcaoTravamento().total;
  const uint32_t falhasAntes = encoderLer(1).falhas;

  // Movimento longo, para o pulso estar claramente correndo.
  webPost("/api/mover?t1=60&t2=0");
  rodarComWeb(200);

  // O DRIVER EMUDECE por 700 ms: mais que o meio segundo do vigia, menos
  // que o segundo que derruba a leitura. E exatamente essa faixa que
  // acusava travamento sem eixo nenhum ter travado.
  g_uart.escravo[0].mudo = true;
  rodarComWeb(700);
  const uint32_t travDurante = correcaoTravamento().total;
  const uint32_t falhas = encoderLer(1).falhas - falhasAntes;
  g_uart.escravo[0].mudo = false;

  uint32_t t = 0;
  while (modoAtual != MODO_MANUAL && t < 15000) { rodarComWeb(20); t += 20; }

  nota("700 ms de barramento mudo com o pulso correndo: %lu falha(s), "
       "travamentos %lu; encoder terminou em %.2f",
       (unsigned long)falhas, (unsigned long)(travDurante - travAntes),
       (double)encoderLer(1).graus);

  checar(falhas > 0, "M13a",
         "o barramento realmente emudeceu -- sem isso o cenario nao prova nada");
  checar(travDurante == travAntes, "M13b",
         "e ausencia de medida NAO acusa travamento: a janela do vigia so "
         "vale cheia de amostras que chegaram");
  checar(fabsf(encoderLer(1).graus - 60.0f) < 0.5f, "M13c",
         "o movimento segue e chega, em vez de morrer no meio do cordao");
}

// ---------------------------------------------------------------------
// M14: eixo rapido de verdade nao pode virar salto recusado.
//
// A guarda de salto tinha um teto FIXO de tres voltas de motor por
// segundo. Numa junta com reducao alta isso e pouco: a 20 graus/s com
// reducao 50 o motor ja gira 2,8 voltas/s. Movimento normal caia como
// leitura impossivel -- e cada recusa zera a velocidade, alimentando o
// vigia de travamento do M13. Um defeito criava o outro.
//
// O firmware SABE quanto mandou o eixo andar: a frequencia de pulso
// dividida pelos pulsos por volta da voltas do motor por segundo. O teto
// fixo virou piso, para o braco empurrado a mao, que nao aparece em
// frequencia de pulso nenhuma.
// ---------------------------------------------------------------------
static void teste_M14_eixo_rapido_nao_e_salto() {
  secao("M14  Eixo rapido de verdade passa pela guarda de salto");
  reiniciarSistema();

  // Reducao alta e velocidade cheia: o motor passa MUITO das tres voltas
  // por segundo que o teto fixo permitia. A reducao entra antes do
  // preparador porque o curso em graus e derivado dela.
  J1.reducao = 100.0f;
  recalcularResolucao();
  velAuto = 60.0f;
  // Aceleracao de bancada nao serve aqui: com os 60 graus/s2 padrao um
  // movimento curto e todo rampa e o eixo nunca chega na velocidade de
  // cruzeiro -- o pico ficava em 2,6 voltas/s, ABAIXO do teto antigo, e o
  // cenario passava a verde sem exercitar nada.
  J1.aceleracao = 400.0f;

  prepararRoboCalibrado();
  prepararEncoder(90, true, 0);
  rodarComWeb(300);
  enviarComando(CMD_ENCODER_ZERAR, 0);
  rodarComWeb(120);
  g_espelharEixo = true;
  rodarComWeb(200);

  const float voltasPorS = velAuto * J1.reducao / 360.0f;
  nota("junta a %.0f graus/s com reducao %.0f: o motor gira ate %.1f voltas/s, "
       "contra o teto fixo de %.1f", (double)velAuto, (double)J1.reducao,
       (double)voltasPorS, (double)ENC_SALTO_VOLTAS_POR_S);

  const uint32_t saltosAntes = encoderLer(1).saltos;
  irEsperando(80, 0, 25000);
  const uint32_t saltos = encoderLer(1).saltos - saltosAntes;

  const float cvM  = configEncoder.contagensPorVolta[0];
  const float pico = encoderLer(1).velMax / cvM;
  nota("depois do movimento: %lu salto(s) recusado(s), encoder le %.2f, "
       "pico MEDIDO %.2f voltas/s do motor",
       (unsigned long)saltos, (double)encoderLer(1).graus, (double)pico);
  // O pico nominal nao prova nada: rampa e movimento curto podem deixar o
  // eixo abaixo do teto antigo sem ninguem perceber, e o cenario passaria a
  // verde sem exercitar a guarda. Quem tem de passar do teto e o eixo.
  checar(pico > ENC_SALTO_VOLTAS_POR_S, "M14a",
         "o eixo REALMENTE passou do teto fixo antigo -- senao a guarda nem "
         "chega a ser consultada e o cenario nao prova nada");
  checar(saltos == 0, "M14b",
         "nenhuma leitura de eixo rapido foi recusada como impossivel: o "
         "limite acompanha a velocidade comandada, nao um numero fixo");
  checar(fabsf(encoderLer(1).graus - 80.0f) < 0.5f, "M14c",
         "e o braco chega, em vez de ser parado por um vigia alimentado de "
         "leituras recusadas");
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

// ---------------------------------------------------------------------
// P04: a regra de soltar o braco e sobre AS JUNTAS QUE EXISTEM.
//
// Junta solta e nao medida cai pelo proprio peso e a contagem dela nao
// anda: o ponto sairia certo num eixo e errado no outro, que e pior do
// que errado nos dois porque parece plausivel. Essa protecao continua.
//
// Mas ela era cobrada das DUAS juntas sempre, e uma junta que nao esta no
// barramento nao tem peso proprio para cair nem contagem para
// desencontrar. Numa maquina de um eixo so -- ou com o segundo driver
// ainda na bancada -- o modo entrava com torque e o operador tinha de
// posicionar pelas setas para gravar um caminho A MAO. Recusar por causa
// de uma junta que nao existe e proteger o que nao esta la.
// ---------------------------------------------------------------------
static void teste_P04_soltar_olha_as_juntas_que_existem() {
  secao("P04  Soltar o braco depende das juntas presentes, nao do numero dois");
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
         "o modo entra");
  checar(a.bracoSolto && !servosLigados, "P04b",
         "e o braco SOLTA com um eixo so: a junta ausente nao cai nem "
         "desencontra contagem, e gravar a mao e o proposito do modo");

  botao(150);
  nota("toque com o braco solto: %u ponto(s)", (unsigned)progQuantidade());
  checar(progQuantidade() == 1, "P04c",
         "e o toque grava ponto");

  // A PROTECAO CONTINUA onde ela tem sentido: junta PRESENTE no
  // barramento e sem zero ensinado. Essa cai mesmo, e a contagem dela
  // mente.
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoderDasDuasJuntas();   // ensina o zero das duas
  rodarComWeb(300);
  webPost("/api/zero/esquecer?j=2"); // e a junta 2 perde o dela
  rodarComWeb(200);

  botao(APRENDER_SEGURAR_MS + 300);
  const ResumoAprender b = aprenderResumo();
  nota("junta 2 presente e sem zero: solto=%d servos=%d -- \"%s\"",
       (int)b.bracoSolto, (int)servosLigados, ultimaMensagem);
  checar(b.ativo && !b.bracoSolto && servosLigados, "P04d",
         "junta que ESTA no barramento e ninguem mede segura o torque: e "
         "ela que cairia e gravaria ponto torto");
  checar(strstr(ultimaMensagem, "junta 2") != nullptr, "P04e",
         "e a tela nomeia a junta que esta segurando, em vez de o operador "
         "achar que o botao falhou");
}

// ---------------------------------------------------------------------
// P06: um eixo so grava E REPRODUZ.
//
// Gravar a mao com um motor so ja era metade do pedido; a outra metade e
// o caminho gravado voltar a andar. E ali havia uma trava calada:
// moverCoordenado() comecava com
//
//     if (!J1.motor || !J2.motor) return;
//
// -- com um motor so, a reproducao nao movia NADA, nem o eixo que existe,
// e sem uma palavra na tela. Coordenar dois movimentos e o caso comum,
// nao a condicao para haver movimento.
//
// A posicao do eixo ausente e 0, como o relatorio pede -- e ela sai 0
// sozinha, porque posicaoJ2() devolve 0 quando nao ha motor. Ninguem
// precisa forcar, e por isso ninguem corre o risco de forcar errado.
// ---------------------------------------------------------------------
static void teste_P08_um_eixo_so_grava_e_reproduz() {
  secao("P08  Com um eixo so, o caminho grava e volta a andar");
  reiniciarSistema();
  prepararRoboCalibrado(170.0f);
  prepararEncoder(90, true, 500000);   // so a junta 1 no barramento
  rodarComWeb(300);
  webPost("/api/zero/ensinar?j=1&g=0");
  rodarComWeb(200);

  // A maquina de um eixo so: o segundo driver nem existe.
  J2.motor = nullptr;
  enviarComando(CMD_ENCODER_ZERAR, 0);
  rodarComWeb(120);
  // O encoder segue o eixo de verdade. Sem isto ele fica congelado
  // enquanto o pulso corre, e o vigia de travamento para o braco -- com
  // razao. O cenario mediria a protecao, nao a maquina de um eixo.
  g_espelharEixo = true;
  rodarComWeb(50);

  progLimpar();
  const char* m = nullptr;
  const bool p1ok = progAdicionarPonto(grausParaPassos(J1, 10.0f), 0, &m);
  const bool p2ok = progAdicionarPonto(grausParaPassos(J1, 40.0f), 0, &m);
  nota("dois pontos com a junta 2 ausente: %d e %d -- %s",
       (int)p1ok, (int)p2ok, m ? m : "sem recusa");
  checar(p1ok && p2ok, "P08a",
         "os pontos entram com a junta ausente valendo 0, em vez de a "
         "gravacao recusar por causa de um eixo que nao esta la");

  const long antes = posicaoJ1();   // o braco comeca em zero

  // Pela fila de comandos, como a interface faz: progIniciar() sozinho nao
  // poe o robo em MODO_EXECUTANDO, e sem isso o laco nunca chama
  // progAtualizar().
  enviarComando(CMD_PROG_EXECUTAR, 1);   // ensaio, sem arco
  rodarComWeb(60);
  nota("modo=%d progRodando=%d -- \"%s\"", (int)modoAtual,
       (int)progRodando(), ultimaMensagem);
  checar(progRodando() && modoAtual == MODO_EXECUTANDO, "P08b",
         "e o programa parte");

  uint32_t t = 0;
  while (progRodando() && t < 20000) { rodarComWeb(20); t += 20; }
  const float andou = fabsf((float)(posicaoJ1() - antes)) / J1.passosPorGrau;
  nota("depois da reproducao: a junta 1 andou %.1f grau(s), e parou em %.1f",
       (double)andou, (double)passosParaGraus(J1, posicaoJ1()));
  checar(andou > 30.0f, "P08c",
         "a junta que existe percorre o caminho: exigir os dois motores "
         "fazia a maquina de um eixo nao andar nada, e calada");
  if (progRodando()) progParar();
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
// V11: UM controle de velocidade manda nos dois caminhos.
//
// A versao anterior deste cenario prendia o contrato do modo Precisao:
// "ir para um angulo respeita o botao Precisao". O modo nao existe mais
// -- ele era a quarta escala para o mesmo numero, ao lado dos cinco
// degraus e dos apelidos lento/normal/rapido, e trocar de escala era o
// que fazia a velocidade parecer que nao pegava.
//
// O contrato que fica no lugar e o do pedido: um controle so, em mm/s,
// e ele manda IGUAL no jog e no ir-para-angulo. Sao os dois caminhos que
// a mao do operador usa; ate aqui cada um obedecia a um campo diferente
// (velN e velA), guardados em telas diferentes.
// ---------------------------------------------------------------------
static void teste_V11_um_controle_manda_nos_dois() {
  secao("V11  Uma velocidade so: o mesmo numero manda no jog e no angulo");
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoder(90, true, 500000);
  g_espelharEixo = false;
  rodarComWeb(200);

  // Exatamente o que velEnviar() manda: os dois campos com o MESMO
  // valor, e a rampa amarrada nele.
  auto pedirVelocidade = [&](float g) {
    char rota[128];
    snprintf(rota, sizeof(rota),
             "/api/config?velN=%.1f&velA=%.1f&acel1=%.0f&acel2=%.0f",
             (double)g, (double)g, (double)(g / 0.35f), (double)(g / 0.35f));
    webPost(rota);
    rodarComWeb(120);
  };

  auto voltarAoZero = [&]() {
    enviarComando(CMD_IR_HOME);
    uint32_t t = 0;
    while (motoresEmMovimento() && t < 30000) { rodarComWeb(40); t += 40; }
    rodarComWeb(200);
  };

  // Quanto o eixo anda numa janela FIXA, ja em regime.
  //
  // A janela comeca 400 ms depois da largada de proposito: nos primeiros
  // instantes o que se mede e a rampa, nao a velocidade, e a rampa faz o
  // numero depender de onde o eixo estava antes. O alvo fica longe o
  // bastante para nao ser alcancado dentro da janela -- senao o que se
  // mede e a distancia.
  const uint32_t ASSENTAR = 400, JANELA = 400;
  auto porAngulo = [&]() -> long {
    voltarAoZero();
    webPost("/api/mover?t1=60&t2=0");
    rodarComWeb(ASSENTAR);
    const long de = posicaoJ1();
    rodarComWeb(JANELA);
    const long quanto = labs(posicaoJ1() - de);
    enviarComando(CMD_PARAR);
    rodarComWeb(200);
    return quanto;
  };

  // O mesmo, no jog: o comando e repetido como a tela repete enquanto o
  // dedo esta no botao.
  auto porJog = [&]() -> long {
    voltarAoZero();
    for (uint32_t t = 0; t < ASSENTAR; t += 50) {
      enviarComando(CMD_JOG, 1, 1); rodarComWeb(50);
    }
    const long de = posicaoJ1();
    for (uint32_t t = 0; t < JANELA; t += 50) {
      enviarComando(CMD_JOG, 1, 1); rodarComWeb(50);
    }
    const long quanto = labs(posicaoJ1() - de);
    enviarComando(CMD_JOG, 1, 0);
    enviarComando(CMD_PARAR);
    rodarComWeb(200);
    return quanto;
  };

  pedirVelocidade(24.0f);
  const long ang = porAngulo();
  const long jog = porJog();
  const float maior = (float)((ang > jog) ? ang : jog);
  const float dif   = (maior > 0.0f) ? fabsf((float)(ang - jog)) / maior : 1.0f;
  nota("em %u ms de regime a 24 graus/s: ir-para-angulo %ld passos, "
       "jog %ld passos (diferenca %.1f%%)",
       (unsigned)JANELA, ang, jog, (double)(dif * 100.0f));
  checar(ang > 0 && jog > 0 && dif < 0.15f, "V11a",
         "o mesmo numero produz a mesma velocidade de junta nos dois "
         "caminhos: um controle so, e nao um por tela");

  // E ele MANDA: baixar o numero tem de deixar o ir-para-angulo bem mais
  // devagar. E o que o botao Precisao fazia, agora sem um segundo modo.
  pedirVelocidade(6.0f);
  const long devagar = porAngulo();
  nota("em %u ms de regime a 6 graus/s: %ld passos (contra %ld a 24 graus/s)",
       (unsigned)JANELA, devagar, ang);
  checar(devagar > 0 && ang > devagar * 2, "V11b",
         "reduzir a velocidade pedida reduz o ir-para-angulo junto");
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
// V24: curso medido nao pode CALAR o encoder quando o limite esta
//      desligado.
//
// Relato: "movo o motor e o braco nao acompanha o movimento".
//
// leituraPlausivel() recusava qualquer angulo fora do curso medido, sem
// olhar se o operador tinha LIGADO o limite. Desde que o limite virou
// opcao, o curso medido deixou de ser uma afirmacao sobre onde o braco
// pode estar -- o braco anda livre pela mesa por padrao.
//
// O efeito era total: uma calibracao abortada deixava um curso de dois
// graus, o braco parava em -65 graus, e TODA leitura do encoder passava
// a ser descartada. Sem encoder nao havia reancoragem, nem seguimento de
// eixo solto, nem assentamento -- e o desenho na tela, que so obedece ao
// encoder, congelava.
// ---------------------------------------------------------------------
static void teste_V24_curso_medido_nao_cala_o_encoder() {
  secao("V24  Curso medido nao cala o encoder com o limite desligado");
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoderDasDuasJuntas();
  rodarComWeb(300);

  // Uma calibracao que parou no meio: a junta 1 fica com um curso
  // ridiculo de dois graus, e "calibrada" mesmo assim.
  const long dois = (long)(2.0f * J1.passosPorGrau);
  J1.passosMin = -dois; J1.passosMax = dois;
  recalcularResolucao();
  rodarComWeb(50);

  // A escala medida do encoder: contagens por grau DA JUNTA, que e o que
  // a calibracao guiada ensina. Sem ela a leitura sairia pela reducao de
  // catalogo e o cenario nao teria escala nenhuma para posicionar.
  const float redJ1 = (J1.reducao > 0.001f) ? J1.reducao : 1.0f;
  const float cpg   = configEncoder.contagensPorVolta[0] * redJ1 / 360.0f;
  configEncoder.contagensPorGrau[0] = cpg;
  rodarComWeb(50);

  // E o braco esta bem longe dali -- 40 graus, que e onde a mao o deixou.
  const float LONGE = 40.0f;
  g_uart.escravo[0].posicao = (int32_t)(500000 + LONGE * cpg);
  rodarComWeb(400);

  const LeituraEncoder L = encoderLer(1);
  nota("curso medido da junta 1: %.1f a %.1f graus; encoder lendo %.2f",
       (double)J1.grausMin, (double)J1.grausMax, (double)L.graus);

  // Com o LIMITE LIGADO a conferencia continua valendo: e o operador
  // dizendo "este curso e real, respeite-o".
  protCurso = true;
  rodarComWeb(100);
  checar(!leituraConfiavel(1), "V24a",
         "com o limite LIGADO, leitura fora do curso medido segue recusada");

  // Com o limite DESLIGADO -- o padrao da maquina -- o braco anda livre,
  // e leitura fora daquela faixa e leitura boa de onde ele de fato esta.
  protCurso = false;
  rodarComWeb(100);
  nota("limite desligado: confiavel=%d", (int)leituraConfiavel(1));
  checar(leituraConfiavel(1), "V24b",
         "com o limite DESLIGADO o encoder volta a valer: curso medido nao "
         "e fronteira quando a maquina anda livre");

  // E o status tem de dizer isso, porque e m1ok que manda a tela desenhar.
  webGet("/api/status");
  const std::string js = webCorpo();
  checar(js.find("\"m1ok\":true") != std::string::npos, "V24c",
         "e o status diz m1ok:true, para o desenho voltar a seguir o braco");

  // O teto absoluto continua valendo com o limite desligado: ele e que
  // separa leitura de lixo, e nao depende de opcao nenhuma.
  g_uart.escravo[0].posicao = 500000000;
  rodarComWeb(400);
  nota("com lixo no barramento e limite desligado: %.0f graus, confiavel=%d",
       (double)encoderLer(1).graus, (int)leituraConfiavel(1));
  checar(!leituraConfiavel(1), "V24d",
         "angulo absurdo segue recusado mesmo com o limite desligado");
}

// ---------------------------------------------------------------------
// V25: ir a um angulo tem de partir de onde o BRACO esta.
//
// Relato: "quando clico para ir ao ponto zero ou a um angulo ele esta se
// baseando no erro para a chegada; o braco nao chega ao angulo, so o
// erro".
//
// O destino de "ir para o zero" e absoluto EM PULSOS, calculado sobre a
// contagem do firmware. Com a contagem adiantada do braco -- perda de
// passo, folga, escala recem-medida -- mover a contagem ate o alvo deixa
// o braco parado no tanto do erro. Na tela isso e literal: o fantasma
// tracejado (que E a contagem) chega ao angulo pedido, e o braco
// desenhado nao.
// ---------------------------------------------------------------------
// O eixo segue os PULSOS: ele anda o mesmo que a contagem andou, e a
// diferenca que ja existia entre os dois continua existindo. Colar o
// encoder na contagem, como faz colarEncoderNaContagem(), apagaria de
// graca justamente o erro que este cenario mede -- e o cenario passaria
// com ou sem a correcao.
static float g_v25Erro = 0.0f;
static void v25EixoSegueOsPulsos(float cpg) {
  const float conta = passosParaGraus(J1, posicaoJ1());
  g_uart.escravo[0].parar();
  g_uart.escravo[0].posicao = (int32_t)(500000 + (conta + g_v25Erro) * cpg);
}

static void teste_V25_ir_ao_angulo_parte_de_onde_o_braco_esta() {
  secao("V25  Ir a um angulo parte de onde o BRACO esta, nao da contagem");
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoderDasDuasJuntas();
  rodarComWeb(300);

  // A escala medida do encoder -- contagens por grau DA JUNTA, que e o
  // que a calibracao guiada ensina.
  const float redJ1 = (J1.reducao > 0.001f) ? J1.reducao : 1.0f;
  const float cpg   = configEncoder.contagensPorVolta[0] * redJ1 / 360.0f;
  configEncoder.contagensPorGrau[0] = cpg;
  rodarComWeb(50);

  // A contagem diz 5 graus...
  ajustarContagem(J1, grausParaPassos(J1, 5.0f));
  // ...e o braco esta de verdade em 12. Sete graus de erro: e o que o
  // fantasma tracejado desenha na tela.
  const float REAL = 12.0f;
  g_uart.escravo[0].posicao = (int32_t)(500000 + REAL * cpg);
  rodarComWeb(400);

  const float contaAntes = passosParaGraus(J1, posicaoJ1());
  const float bracoAntes = encoderLer(1).graus;
  nota("antes: contagem %.2f graus, braco %.2f graus, erro %.2f",
       (double)contaAntes, (double)bracoAntes,
       (double)(bracoAntes - contaAntes));
  checar(fabsf(contaAntes - 5.0f) < 0.3f && fabsf(bracoAntes - REAL) < 0.3f,
         "V25a", "o cenario monta o erro: contagem em 5, braco em 12");

  // "Ir para o zero da maquina".
  enviarComando(CMD_IR_HOME);
  rodarComWeb(30);

  // A contagem foi ancorada ANTES de o destino ser calculado: ela agora
  // descreve o braco. Sem isto ela sairia de 5, o eixo andaria 5 graus e
  // o braco pararia em 7 -- exatamente o tanto do erro.
  const float contaPartida = passosParaGraus(J1, posicaoJ1());
  // O erro que sobrou depois do ancoramento. Ancorada, a contagem
  // descreve o braco e nao sobra nada; sem ancorar, sobram os sete graus
  // -- e e com eles que o eixo vai andar.
  g_v25Erro = encoderLer(1).graus - contaPartida;
  nota("ao mandar ir ao zero, a contagem partiu de %.2f graus (erro que "
       "sobrou: %.2f) -- \"%s\"",
       (double)contaPartida, (double)g_v25Erro, ultimaMensagem);
  checar(fabsf(contaPartida - REAL) < 0.3f, "V25b",
         "a contagem foi ancorada no encoder antes de calcular o destino");

  // E a maquina diz, em vez de reescrever a posicao em silencio.
  checar(strstr(ultimaMensagem, "acertada pelo encoder") != nullptr, "V25c",
         "e avisa que acertou a contagem, em vez de mudar o numero calado");

  // Dali em diante o eixo anda junto com a contagem, guardando o erro
  // que sobrou. Sem mexer no encoder, ele ficaria parado enquanto o eixo
  // anda e o vigia de travamento cortaria o movimento -- com razao.
  uint32_t t = 0;
  while (motoresEmMovimento() && t < 20000) {
    v25EixoSegueOsPulsos(cpg);
    rodarComWeb(40);
    t += 40;
  }
  v25EixoSegueOsPulsos(cpg);
  rodarComWeb(200);

  const float contaFim = passosParaGraus(J1, posicaoJ1());
  const float bracoFim = encoderLer(1).graus;
  nota("chegou: contagem %.2f, braco %.2f (andou %.2f graus desde %.2f)",
       (double)contaFim, (double)bracoFim,
       (double)(contaFim - contaPartida), (double)contaPartida);
  checar(fabsf(contaFim) < 0.5f, "V25d",
         "a contagem chega no zero pedido");
  checar(fabsf(bracoFim) < 0.5f, "V25e",
         "e o BRACO chega junto -- antes ele parava no tanto do erro");

  // Sem erro nenhum, ancorar nao pode mexer em nada nem encher a tela.
  g_v25Erro = 0.0f;
  g_uart.escravo[0].posicao = (int32_t)(500000 + 0.0f * cpg);
  rodarComWeb(400);
  ajustarContagem(J1, grausParaPassos(J1, 0.0f));
  rodarComWeb(50);
  definirMensagem("nada");
  enviarComando(CMD_MOVER_ANGULOS, 0, 0, 10.0f, 0.0f);
  rodarComWeb(30);
  nota("sem erro: \"%s\"", ultimaMensagem);
  checar(strstr(ultimaMensagem, "acertada pelo encoder") == nullptr, "V25f",
         "com a contagem ja certa, o aviso nao aparece");
}

// ---------------------------------------------------------------------
// V26: "ir a um angulo" diz EM QUE CONTA saiu.
//
// Ir pela medida do encoder e ir pela contagem de pulsos sao duas coisas
// diferentes, e so uma delas leva o BRACO ao angulo pedido quando as
// duas divergem. O ancoramento devolvia so um numero, entao "nao havia
// o que corrigir" e "nao consegui ler o encoder" saiam iguais: a mesma
// frase de sempre, o movimento pela contagem, e o braco parando longe.
// ---------------------------------------------------------------------
// A saude diz QUAL firmware esta rodando. Sem isso, um defeito ja
// corrigido no fonte continua aparecendo na bancada e nao ha como saber
// se aquela placa tem ou nao a correcao -- foi exatamente o que
// aconteceu, e custou uma rodada inteira de diagnostico do lado errado.
static void teste_V27_saude_diz_qual_firmware_esta_rodando() {
  secao("V27  A maquina diz qual binario esta gravado nela");
  reiniciarSistema();
  rodarComWeb(200);

  webGet("/api/saude");
  const std::string js = webCorpo();
  const size_t onde = js.find("\"fw\"");
  nota("saude: %s", onde == std::string::npos
       ? "sem campo fw" : js.substr(onde, 34).c_str());
  checar(onde != std::string::npos, "V27a",
         "a saude publica um carimbo do firmware gravado");
  checar(js.find(std::string("\"fw\":\"") + ESP.getSketchMD5() + "\"") != std::string::npos,
         "V27b",
         "e o carimbo e o MD5 do binario gravado -- muda quando o binario "
         "muda, e so quando ele muda");
}

static void teste_V26_diz_em_que_conta_o_movimento_saiu() {
  secao("V26  Ir a um angulo diz se foi pelo encoder ou pela contagem");
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoderDasDuasJuntas();
  rodarComWeb(300);

  const float redJ1 = (J1.reducao > 0.001f) ? J1.reducao : 1.0f;
  configEncoder.contagensPorGrau[0] =
      configEncoder.contagensPorVolta[0] * redJ1 / 360.0f;
  rodarComWeb(50);

  // 1. Encoder respondendo e contagem ja certa: diz que foi medido.
  enviarComando(CMD_MOVER_ANGULOS, 0, 0, 5.0f, 0.0f);
  rodarComWeb(40);
  nota("com encoder bom: \"%s\"", ultimaMensagem);
  checar(strstr(ultimaMensagem, "encoder") != nullptr, "V26a",
         "com leitura boa, a tela diz que o movimento saiu pelo encoder");
  uint32_t t = 0;
  while (modoAtual != MODO_MANUAL && t < 8000) { rodarComWeb(20); t += 20; }

  // 2. O cabo cai. O movimento ainda acontece -- recusar deixaria o
  //    operador sem jeito de tirar o braco do lugar -- mas a tela para
  //    de dizer a mesma frase de sempre.
  g_uart.escravo[0].mudo = true;
  g_uart.escravo[1].mudo = true;
  rodarComWeb(1500);
  enviarComando(CMD_MOVER_ANGULOS, 0, 0, 10.0f, 0.0f);
  rodarComWeb(40);
  nota("com o cabo caido: modo=%d -- \"%s\"", (int)modoAtual, ultimaMensagem);
  checar(modoAtual == MODO_POSICIONANDO, "V26b",
         "sem encoder o movimento ainda acontece: recusar deixaria o "
         "operador sem tirar o braco do lugar");
  checar(strstr(ultimaMensagem, "PELA CONTAGEM") != nullptr, "V26c",
         "mas a tela avisa que ESTE movimento nao e baseado na medida");

  // 3. Maquina sem encoder nenhum nao e falha: e a instalacao que se
  //    escolheu, e ali nao ha o que avisar.
  reiniciarSistema();
  prepararRoboCalibrado();
  configEncoder.reg[0] = configEncoder.reg[1] = 0;
  rodarComWeb(200);
  enviarComando(CMD_IR_HOME);
  rodarComWeb(40);
  nota("maquina sem encoder: \"%s\"", ultimaMensagem);
  checar(strstr(ultimaMensagem, "PELA CONTAGEM") == nullptr &&
         strstr(ultimaMensagem, "Indo para") != nullptr, "V26d",
         "maquina sem encoder nenhum nao leva aviso: operar pela contagem "
         "ali e escolha da instalacao, nao falha");
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
  // por volta do que esta escrito aqui. O FERRO continua sendo o de
  // verdade -- e essa a discordancia inteira: o eixo obedece a engrenagem
  // que o drive tem, e o firmware faz conta com a que alguem digitou.
  const uint32_t ppvVerdadeiro = J1.passosPorVolta;
  g_ppvReal[0] = ppvVerdadeiro;
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

  // SEM regua medida o vigia continua pegando o eixo REALMENTE parado.
  //
  // O criterio muda de forma: com escala medida ele exige proporcao ("o
  // eixo entrega menos de um quinto do que deveria"); sem ela, exige o
  // sinal que independe de escala -- pulso claramente correndo e encoder
  // claramente parado. Eixo que gira produz contagem, seja qual for a
  // escala, entao aqui nao ha falso positivo por numero errado.
  correcaoLimparTravamento();
  configEncoder.contagensPorGrau[0] = 0.0f;
  rodarComWeb(50);
  const uint32_t travAntes2 = correcaoTravamento().total;
  const float preso2 = passosParaGraus(J1, posicaoJ1());
  const int32_t travado2 = g_uart.escravo[0].posicao;
  moverCoordenado(grausParaPassos(J1, preso2 + 30.0f), posicaoJ2(), 20.0f);
  for (int k = 0; k < 200 && motoresEmMovimento(); k++) {
    g_uart.escravo[0].parar();
    g_uart.escravo[0].posicao = travado2;   // eixo preso
    rodarComWeb(10);
  }
  rodarComWeb(100);
  nota("sem escala medida, eixo preso: travamentos=%u -- \"%s\"",
       (unsigned)(correcaoTravamento().total - travAntes2), ultimaMensagem);
  checar(correcaoTravamento().total > travAntes2, "V18e",
         "eixo parado com o pulso correndo e pego mesmo sem escala medida: "
         "esse sinal nao depende de escala nenhuma");

  // E o braco ANDANDO nunca e acusado, com ou sem escala -- que era o
  // defeito: numero de catalogo errado virava eixo travado.
  correcaoLimparTravamento();
  g_espelharEixo = true;
  rodarComWeb(200);
  const uint32_t travAntes3 = correcaoTravamento().total;
  const float parte3 = passosParaGraus(J1, posicaoJ1());
  enviarComando(CMD_MOVER_ANGULOS, 0, 0, parte3 + 25.0f,
                passosParaGraus(J2, posicaoJ2()));
  rodarComWeb(120);
  uint32_t t3 = 0;
  while (motoresEmMovimento() && t3 < 30000) { rodarComWeb(40); t3 += 40; }
  nota("sem escala medida, eixo andando: travamentos=%u, chegou em %.1f",
       (unsigned)(correcaoTravamento().total - travAntes3),
       (double)passosParaGraus(J1, posicaoJ1()));
  checar(correcaoTravamento().total == travAntes3 &&
         fabsf(passosParaGraus(J1, posicaoJ1()) - (parte3 + 25.0f)) < 1.5f, "V18f",
         "e braco andando nunca e acusado, com ou sem escala: eixo que gira "
         "produz contagem, e e isso que o criterio olha");

  pararSuave();
  rodarComWeb(200);
  J1.passosPorVolta = ppvVerdadeiro;
  configEncoder.contagensPorGrau[0] = 0.0f;
  correcaoLimparTravamento();
  g_espelharEixo = false;
}

// ---------------------------------------------------------------------
// V19: calibrar sao dois gestos, e a maquina faz o resto.
//
// "1 - Clico no botao, o braco vai ao 0 e desliga os motores. 2 - movo o
// braco em um sentido ate seu limite maximo, nessa etapa posso ja
// calibrar o eixo 2 junto tambem ja que ele esta solto. 4 - Clico
// guardar, ai ele ativa os motores e volta ao ponto 0 os dois. 5 - ao
// chegar ao 0 ele desativa novamente e faco o mesmo processo do outro
// lado."
// ---------------------------------------------------------------------
static void teste_V19_calibrar_em_dois_gestos() {
  secao("V19  Calibrar sao dois gestos; a maquina faz as viagens");
  reiniciarSistema();
  prepararEncoderDasDuasJuntas();
  // Um redutor de verdade: com 1:1 o motor mal da um quarto de volta no
  // curso inteiro, e nenhuma das medidas automaticas tem base.
  prepararConfigPendente();
  configPendente.red1 = 16.5f; configPendente.red2 = 16.5f;
  enviarComando(CMD_APLICAR_CONFIG);
  rodarComWeb(300);
  g_espelharEixo = false;

  enviarComando(CMD_SERVOS, 1, 0);
  rodarComWeb(300);
  // Longe do zero: a primeira viagem tem de ter o que percorrer.
  if (J1.motor) J1.motor->setCurrentPosition(grausParaPassos(J1, 40.0f));
  if (J2.motor) J2.motor->setCurrentPosition(grausParaPassos(J2, 25.0f));
  colarEncoderNaContagem();
  rodarComWeb(50);
  const uint32_t ppvAntes = J1.passosPorVolta;

  // Espera uma etapa com o encoder colado na contagem: e o que um encoder
  // de verdade faz enquanto a MAQUINA anda.
  auto ateEtapa = [&](EstadoCalib alvo) {
    uint32_t t = 0;
    while (estadoCalib != alvo && t < 40000) {
      colarEncoderNaContagem();
      rodarComWeb(10); t += 10;
    }
    return estadoCalib == alvo;
  };

  // GESTO 1: tocar. A maquina leva os dois ao zero e solta.
  enviarComando(CMD_CALIB_INICIAR);
  const bool parou1 = ateEtapa(CAL_LADO_A);
  nota("depois do primeiro toque: etapa=%d, J1 em %.2f graus, torque 1=%d 2=%d",
       (int)estadoCalib, (double)passosParaGraus(J1, posicaoJ1()),
       (int)J1.habilitado, (int)J2.habilitado);
  checar(parou1 && fabsf(passosParaGraus(J1, posicaoJ1())) < 1.5f &&
         !J1.habilitado && !J2.habilitado, "V19a",
         "um toque: a maquina leva os dois eixos ao zero e SOLTA os motores");

  // O operador empurra os DOIS eixos ate um extremo, com a mao. Com o
  // motor solto, quem manda e o encoder: a contagem vai atras.
  const float cv = configEncoder.contagensPorVolta[0];
  auto empurrar = [&](uint8_t k, float graus) {
    const Junta& j = (k == 1) ? J1 : J2;
    const float red = (j.reducao > 0.001f) ? j.reducao : 1.0f;
    g_uart.escravo[k - 1].parar();
    g_uart.escravo[k - 1].posicao +=
        (int32_t)lroundf((graus * red / 360.0f) * cv);
    rodarComWeb(400);
  };
  empurrar(1, +70.0f);
  empurrar(2, +45.0f);
  nota("empurrado com a mao: J1 em %.1f, J2 em %.1f graus",
       (double)passosParaGraus(J1, posicaoJ1()),
       (double)passosParaGraus(J2, posicaoJ2()));
  checar(fabsf(passosParaGraus(J1, posicaoJ1()) - 70.0f) < 4.0f &&
         fabsf(passosParaGraus(J2, posicaoJ2()) - 45.0f) < 4.0f, "V19b",
         "com os motores soltos a contagem dos DOIS eixos e puxada pelo "
         "encoder: da para calibrar os dois na mesma ida");

  // GESTO 2: tocar. Ela energiza, volta os dois ao zero e solta de novo.
  enviarComando(CMD_CALIB_CONFIRMAR);
  const bool parou2 = ateEtapa(CAL_LADO_B);
  nota("depois do segundo toque: etapa=%d, J1 em %.2f graus, torque 1=%d",
       (int)estadoCalib, (double)passosParaGraus(J1, posicaoJ1()),
       (int)J1.habilitado);
  checar(parou2 && fabsf(passosParaGraus(J1, posicaoJ1())) < 1.5f &&
         !J1.habilitado, "V19c",
         "o segundo toque energiza, volta os dois ao zero e solta de novo");

  // O outro extremo, tambem com a mao.
  empurrar(1, -50.0f);
  empurrar(2, -35.0f);
  enviarComando(CMD_CALIB_CONFIRMAR);
  const bool fim = ateEtapa(CAL_INATIVO);

  nota("fim: J1 de %.1f a %.1f, J2 de %.1f a %.1f graus -- \"%s\"",
       (double)J1.grausMin, (double)J1.grausMax,
       (double)J2.grausMin, (double)J2.grausMax, ultimaMensagem);
  checar(fim && modoAtual == MODO_MANUAL, "V19d",
         "dois gestos e acabou: nada digitado, nenhuma etapa a mais");
  checar(J1.calibrada && J2.calibrada &&
         fabsf((J1.grausMax - J1.grausMin) - 120.0f) < 8.0f &&
         fabsf((J2.grausMax - J2.grausMin) - 80.0f) < 8.0f, "V19e",
         "o curso das DUAS juntas sai dos batentes, na mesma calibracao");

  nota("terminou em %.2f graus, torque 1=%d 2=%d",
       (double)passosParaGraus(J1, posicaoJ1()),
       (int)J1.habilitado, (int)J2.habilitado);
  checar(fabsf(passosParaGraus(J1, posicaoJ1())) < 1.5f && J1.habilitado, "V19f",
         "e a maquina termina no zero, COM torque -- pronta para trabalhar");

  // Os PULSOS POR VOLTA sao medidos nas viagens: pulso contado de um
  // lado, voltas do motor do outro, e o redutor cancela.
  nota("pulsos por volta: declarado %lu -> medido %lu",
       (unsigned long)ppvAntes, (unsigned long)J1.passosPorVolta);
  checar(labs((long)J1.passosPorVolta - (long)ppvAntes) <
         (long)(ppvAntes / 20), "V19h",
         "as viagens ao zero medem os pulsos por volta de cada driver, sem "
         "ninguem pedir -- e o numero bate com o que o driver faz");

  // A escala do encoder sai da propria medida.
  nota("escala medida na junta 1: %.2f contagens por grau",
       (double)configEncoder.contagensPorGrau[0]);
  const float esperada = cv * J1.reducao / 360.0f;
  checar(fabsf(configEncoder.contagensPorGrau[0] - esperada) < esperada * 0.08f,
         "V19g",
         "e a escala do encoder sai de graca: entre os dois extremos ha um "
         "tanto de contagens e um tanto de graus");
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

// ---------------------------------------------------------------------
// V21: mexer na tela nao pode cortar um movimento em curso.
//
// "quando clico em algo ela atualiza informacao, e dessa forma se tem
// algo rodando ele acaba cortando, por exemplo quando clico para ir para
// o 0 ele anda tipo 2 seg e trava o movimento."
//
// A tela toca dezenas de rotas enquanto o braco anda -- consulta de
// estado, de calibracao, do encoder, e as acoes dos botoes que ficam
// visiveis. Nenhuma delas, exceto a PARADA, pode encostar no movimento.
// Este cenario varre a lista inteira e denuncia quem encostar.
// ---------------------------------------------------------------------
static void teste_V21_a_tela_nao_corta_o_movimento() {
  secao("V21  Mexer na tela nao corta um movimento em curso");

  // Rotas que a tela pode disparar com o braco andando. A PARADA fica de
  // fora de proposito: parar e o trabalho dela.
  static const char* ROTAS_TELA[] = {
    "/api/config?velN=30&velA=30&acel1=90&acel2=90",
    "/api/jog?j=1&d=0",
    "/api/jog?j=2&d=0",
    "/api/jogxy?f1=0&f2=0",
    "/api/teste/rele",
    "/api/referenciar",
    "/api/ponto/gravar",
    "/api/mesa/canto",
    "/api/correcao?on=1",
    "/api/encoder/config?per=50",
    "/api/geometria?l1=450&l2=400",
    "/api/sentido?j=1&v=0",
    "/api/aprender?v=0",
    "/api/prog/pausar?v=0",
    "/api/travamento/ok",
    "/api/manutencao/ok",
  };
  const size_t N = sizeof(ROTAS_TELA) / sizeof(ROTAS_TELA[0]);

  for (size_t r = 0; r < N; r++) {
    reiniciarSistema();
    prepararRoboCalibrado();
    rodarComWeb(60);

    // Um percurso longo: da para tocar a rota no meio dele com folga.
    if (J1.motor) J1.motor->setCurrentPosition(grausParaPassos(J1, -60.0f));
    rodarComWeb(20);
    enviarComando(CMD_MOVER_ANGULOS, 0, 0, 60.0f, 0.0f);
    rodarComWeb(200);
    if (!motoresEmMovimento()) {
      nota("%s: o movimento nem comecou -- cenario invalido", ROTAS_TELA[r]);
      continue;
    }

    webPost(ROTAS_TELA[r]);
    rodarComWeb(300);
    const bool andando = motoresEmMovimento();
    if (!andando) {
      nota("CORTOU: %s -- modo=%d, \"%s\"", ROTAS_TELA[r], (int)modoAtual,
           ultimaMensagem);
    }
    checar(andando, "V21",
           "a tela nao corta o movimento em curso");
    if (!andando) break;   // um so ja basta para o operador sentir
  }
  nota("%u rota(s) de tela varridas com o braco andando", (unsigned)N);
}

// ---------------------------------------------------------------------
// V22: o assentamento passa pelo mesmo portao que o resto.
//
// Ele e o unico caminho do firmware que move o braco sem ter vindo de um
// comando do operador. pararTudo() ja o cancela, mas depender disso e
// confiar em ordem de chamada: se um dia alguem cortar o movimento por
// outro caminho, o retoque daria mais um passo depois.
// ---------------------------------------------------------------------
static void teste_V22_assentamento_respeita_o_portao() {
  secao("V22  O assentamento nao anda com o portao de seguranca fechado");
  reiniciarSistema();
  prepararRoboCalibrado();
  prepararEncoderDasDuasJuntas();
  rodarComWeb(300);
  g_espelharEixo = false;

  // Um desvio grande o bastante para o assentamento querer retocar.
  const float onde = passosParaGraus(J1, posicaoJ1());
  const float cv   = configEncoder.contagensPorVolta[0];
  const float red  = (J1.reducao > 0.001f) ? J1.reducao : 1.0f;
  g_uart.escravo[0].parar();
  g_uart.escravo[0].posicao =
      encoderLer(1).referencia +
      (int32_t)lroundf((((onde - 1.0f) - J1.grausHome) * red / 360.0f) * cv);
  rodarComWeb(300);

  // Portao fechado por uma causa de verdade: o painel some. Por a
  // variavel na mao nao serve -- o supervisor a recalcula todo ciclo.
  // (rodar() sem o "ComWeb" e exatamente isso: nenhum contato HTTP.)
  rodar(TIMEOUT_CONEXAO_MS + 400);
  nota("painel sumido: movimentoSeguro=%d, modo=%d",
       (int)movimentoSeguro, (int)modoAtual);

  const long antes = posicaoJ1();
  correcaoIniciar();
  for (int k = 0; k < 200; k++) { correcaoAtualizar(); rodar(10); }

  nota("portao fechado: contagem %ld -> %ld, estado %d -- \"%s\"",
       antes, posicaoJ1(), (int)correcaoResumo().estado, ultimaMensagem);
  checar(!movimentoSeguro && posicaoJ1() == antes && !motoresEmMovimento(),
         "V22a", "com o portao fechado o retoque nao move o braco");
  checar(correcaoResumo().estado == CORR_PARADA, "V22b",
         "e o assentamento se declara parado, em vez de ficar tentando");

  // Reaberto o portao -- painel de volta e servos rearmados -- ele volta
  // a fazer o trabalho dele.
  rodarComWeb(400);
  enviarComando(CMD_SERVOS, 1);
  rodarComWeb(300);
  // O desvio e reposto: o que se testa e o portao, nao a memoria do
  // desvio anterior.
  g_uart.escravo[0].parar();
  g_uart.escravo[0].posicao =
      encoderLer(1).referencia +
      (int32_t)lroundf((((passosParaGraus(J1, posicaoJ1()) - 1.0f) - J1.grausHome)
                        * red / 360.0f) * cv);
  rodarComWeb(300);
  const long antes2 = posicaoJ1();
  correcaoIniciar();
  bool retocou = false;
  for (int k = 0; k < 400; k++) {
    correcaoAtualizar(); rodarComWeb(10);
    if (correcaoEmCurso() || posicaoJ1() != antes2) { retocou = true; break; }
  }
  nota("portao aberto: movimentoSeguro=%d, estado %d, contagem %ld -> %ld",
       (int)movimentoSeguro, (int)correcaoResumo().estado, antes2, posicaoJ1());
  checar(movimentoSeguro && retocou &&
         correcaoResumo().estado != CORR_PARADA, "V22c",
         "reaberto o portao, o assentamento volta a valer");
  pararSuave();
  rodarComWeb(100);
}

// ---------------------------------------------------------------------
// V23: a maquina de fabrica anda livre pela mesa.
//
// "o braco ele deve se mover de forma livre pela mesa desconsiderando
// limites, a menos que nas configuracoes seja ativado limite."
//
// Protecao ligada sobre numero errado nao protege nada: ela recusa
// movimento legitimo e o operador fica olhando um braco que nao anda,
// sem saber por que. Ligada, ela vale -- e quem liga sabe o que esta
// protegendo.
// ---------------------------------------------------------------------
static void teste_V23_maquina_nasce_livre() {
  secao("V23  De fabrica o braco anda livre; limite e opcao");
  reiniciarSistema();
  rodarComWeb(50);

  nota("de fabrica: curso=%d, dobra=%d, envelope=%d",
       (int)protCurso, (int)protDobra, (int)protEnvelope);
  checar(!protCurso && !protDobra && !protEnvelope, "V23a",
         "as tres protecoes nascem desligadas");

  // Com limites gravados e a protecao desligada, uma postura fora do
  // curso continua valendo: o limite existe, mas ninguem mandou impor.
  J1.calibrada = J2.calibrada = true;
  const long p = (long)(30.0f * J1.passosPorGrau);
  J1.passosMin = -p; J1.passosMax = p;
  J2.passosMin = -p; J2.passosMax = p;
  recalcularResolucao();
  rodarComWeb(20);
  nota("curso gravado: J1 de %.0f a %.0f graus", J1.grausMin, J1.grausMax);
  checar(posturaValida(80.0f, 0.0f, nullptr), "V23b",
         "curso medido e protecao desligada: 80 graus continua valendo, "
         "mesmo com o limite gravado em 30");

  // Ligada na configuracao, ela passa a valer na hora.
  const int cod = webPost("/api/protecoes?curso=1&dobra=0&env=0");
  rodarComWeb(120);
  nota("depois de ligar pela rota: HTTP %d, curso=%d", cod, (int)protCurso);
  checar(cod == 200 && protCurso && !posturaValida(80.0f, 0.0f, nullptr), "V23c",
         "ligado o limite na configuracao, ele passa a recusar na hora");

  // E desligar devolve a liberdade, sem apagar o curso medido.
  webPost("/api/protecoes?curso=0&dobra=0&env=0");
  rodarComWeb(120);
  checar(!protCurso && posturaValida(80.0f, 0.0f, nullptr) &&
         fabsf(J1.grausMax - 30.0f) < 1.0f, "V23d",
         "e desligar devolve a liberdade sem apagar o curso medido");
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
  checar(cod == 200 && aprenderResumo().ativo, "P07a",
         "da para entrar no aprendizado pela tela, sem botao fisico nenhum");

  webGet("/api/status");
  const std::string js = webCorpo();
  const size_t onde = js.find("\"aprBotao\"");
  nota("status: %s", js.substr(onde == std::string::npos ? 0 : onde, 62).c_str());
  checar(js.find("\"apr\":true") != std::string::npos &&
         js.find("\"aprSolto\":true") != std::string::npos, "P07b",
         "e o status diz que o braco esta solto -- a tela nao precisa adivinhar");

  // Gravar pela tela dentro do modo conta na sessao, igual ao botao.
  levarComAMao(12.0f, 12.0f);
  webPost("/api/ponto/gravar");
  rodarComWeb(200);
  nota("gravar pela tela: sessao=%u ponto(s), programa=%u",
       (unsigned)aprenderResumo().gravados, (unsigned)progQuantidade());
  checar(aprenderResumo().gravados == 1 && progQuantidade() == 1, "P07c",
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
    f.println("velP=3");   // chave do modo precisao, que nao existe mais
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
  "/api/manutencao/ok", "/api/mesa/canto", "/api/mesa/limpar",
  "/api/mover", "/api/mover_xy",
  "/api/parar", "/api/ponto/gravar", "/api/ponto/ir", "/api/ponto/remover",
  "/api/ponto/solda", "/api/prog/desenho",
  "/api/prog/desfazer", "/api/prog/executar", "/api/prog/limpar",
  "/api/prog/parar", "/api/prog/pausar", "/api/prog/repetir",
  "/api/protecoes", "/api/referenciar", "/api/reproduzir", "/api/sd/apagar",
  "/api/sd/carregar", "/api/sd/montar", "/api/sd/prever", "/api/sd/salvar",
  "/api/sentido", "/api/servos", "/api/solda", "/api/son/config",
  "/api/teste/rele",
  "/api/traj/limpar", "/api/travamento/ok", "/api/zero/config",
  "/api/zero/ensinar", "/api/zero/esquecer"
};
// Rotas de POST deixadas de fora de proposito, com o motivo. A lista
// abaixo e conferida contra o firmware em S01b: rota nova que ninguem
// varreu e rota que ninguem testou com lixo.
// Lida pelo conferir_rotas.py, nao pelo C++: o (void) e para o
// compilador nao reclamar de uma lista que existe para outra ferramenta.
static const char* ROTAS_POST_FORA[] = {
  "/api/ota",            // sobe firmware: o mock nao tem o que receber
  "/api/cfg/restaurar",  // le do cartao e reescreve a maquina inteira
};
static const size_t nROTAS_POST_FORA =
    sizeof(ROTAS_POST_FORA) / sizeof(ROTAS_POST_FORA[0]);

static const char* ROTAS_GET[] = {
  "/api/encoder", "/api/encoder/teste", "/api/pontos", "/api/rede",
  "/api/registro", "/api/saude", "/api/sd", "/api/sd/lista",
  "/api/sd/previa", "/api/status", "/api/trajetoria"
};

// Cada nome de argumento usado em qualquer rota, para nao depender de
// adivinhar qual rota le qual chave.
static const char* CHAVES[] = {
  "a","b","i","j","v","g","on","conf","ensaio","tipo","nome","senha",
  "atual","nova","l1","l2","dobra","envY","envR","velN","velA",
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
  (void)nROTAS_POST_FORA;
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
  // (o alarme do driver saiu do firmware; aqui a falha vem do caminho que
  //  restou -- a parada do operador com o sistema ja em movimento.)
  webPost("/api/parar");
  rodarComWeb(300);
  varrer("logo depois de uma parada");

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
  uint32_t tA = 0;
  while (estadoCalib != CAL_LADO_A && tA < 20000) { rodarComWeb(20); tA += 20; }
  nota("modo=%d etapa=%d (CAL_LADO_A=%d)", (int)modoAtual, (int)estadoCalib,
       (int)CAL_LADO_A);
  checar(modoAtual == MODO_CALIBRANDO && estadoCalib == CAL_LADO_A, "K02a",
         "a maquina leva o braco ao zero, solta os motores e para na "
         "primeira parada");

  // E exatamente aqui que o operador aperta a seta e ve o braco ir para o
  // outro lado. Mandar cancelar a calibracao para consertar era pedir
  // para ele desistir.
  const int cod = webPost("/api/sentido?j=1&v=1");
  rodarComWeb(200);
  nota("na primeira etapa: HTTP %d -- \"%s\"", cod, ultimaMensagem);
  checar(cod == 200 && J1.inverterDir, "K02b",
         "na primeira etapa da para inverter sem cancelar a calibracao");

  // Depois de marcar o primeiro extremo, NAO: trocar o sinal do eixo
  // agora inverteria o significado do que ja foi medido.
  J1.motor->setCurrentPosition(grausParaPassos(J1, 60.0f));
  enviarComando(CMD_CALIB_CONFIRMAR);
  uint32_t t = 0;
  while (estadoCalib != CAL_LADO_B && t < 20000) { rodarComWeb(20); t += 20; }
  const bool eraInv = J1.inverterDir;
  const int cod2 = webPost("/api/sentido?j=1&v=0");
  rodarComWeb(120);
  nota("na etapa %d: HTTP %d, inverterDir continua %d -- \"%s\"",
       (int)estadoCalib, cod2, (int)J1.inverterDir, ultimaMensagem);
  checar(estadoCalib == CAL_LADO_B && cod2 == 400 &&
         J1.inverterDir == eraInv, "K02c",
         "depois de marcar o primeiro extremo o sentido trava, com motivo");
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
  teste_E02_o_zero_fica_onde_esta();
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
  teste_I04_deslocamento_tambem_e_reta();

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
  teste_M06_chega_sem_curso_e_com_erro_grande();
  teste_M07_desiste_quando_nao_aproxima();
  teste_M08_regua_discordante_chega_mesmo_assim();
  teste_M09_regua_errada_nao_inventa_travamento();
  teste_M10_salto_impossivel_nao_vira_posicao();
  teste_M11_maquina_afere_a_propria_engrenagem();
  teste_M12_nao_desiste_por_aritmetica();
  teste_M13_silencio_nao_e_travamento();
  teste_M14_eixo_rapido_nao_e_salto();
  teste_M03_desligado_e_parada();
  teste_M04_travamento_nao_dispara_a_toa();
  teste_M05_seguir_o_eixo_solto();
  teste_N01_ensinar_e_recuperar();
  teste_N02_ir_ao_zero_ao_ligar();
  teste_N03_o_que_impede_de_ir();
  teste_P01_ensinar_com_a_mao();
  teste_P02_um_toque_e_um_ponto();
  teste_P03_toque_fora_do_modo();
  teste_P04_soltar_olha_as_juntas_que_existem();
  teste_P05_o_que_encerra_o_aprendizado();
  teste_P08_um_eixo_so_grava_e_reproduz();
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
  teste_V11_um_controle_manda_nos_dois();
  teste_V12_leitura_absurda_nao_e_confiavel();
  teste_V24_curso_medido_nao_cala_o_encoder();
  teste_V25_ir_ao_angulo_parte_de_onde_o_braco_esta();
  teste_V26_diz_em_que_conta_o_movimento_saiu();
  teste_V27_saude_diz_qual_firmware_esta_rodando();
  teste_V14_velocidade_por_motor();
  teste_V15_ir_a_um_angulo_sem_calibracao();
  teste_V16_contagem_perdida_e_reancorada();
  teste_V17_junta_com_torque_nao_e_seguida();
  teste_V18_vigia_usa_a_escala_medida();
  teste_V19_calibrar_em_dois_gestos();
  teste_V20_junta_muda_nao_rouba_o_barramento();
  teste_V21_a_tela_nao_corta_o_movimento();
  teste_V22_assentamento_respeita_o_portao();
  teste_V23_maquina_nasce_livre();
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
