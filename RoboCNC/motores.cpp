#include "motores.h"
#include "cinematica.h"
#include "solda.h"
#include <math.h>

static FastAccelStepperEngine engine = FastAccelStepperEngine();

static int8_t   jogDir[2]     = {0, 0};
static uint32_t jogUltimoMs[2] = {0, 0};
static float    jogFracao[2]  = {1.0f, 1.0f};
// Ultimo valor realmente programado no gerador de pulso, para nao
// reprogramar a rampa a cada ciclo de 1 ms.
static uint32_t jogHzAplicado[2]      = {0, 0};
static int8_t   jogSentidoAplicado[2] = {0, 0};

// ---------------------------------------------------------------------
static uint32_t limitarFreq(uint32_t v) {
  if (v < 1) return 1;
  if (v > FREQ_PULSO_MAX_HZ) return FREQ_PULSO_MAX_HZ;
  return v;
}

// ---------------------------------------------------------------------
static uint32_t velProgramada[2] = {0, 0};

bool motoresIniciar() {
  J1.pinoPulso = PIN_J1_PULSO; J1.pinoDir = PIN_J1_DIR; J1.pinoAlarme = PIN_ALARME_J1;
  J2.pinoPulso = PIN_J2_PULSO; J2.pinoDir = PIN_J2_DIR; J2.pinoAlarme = PIN_ALARME_J2;

  pinMode(PIN_SERVO_ON, OUTPUT);
  digitalWrite(PIN_SERVO_ON, LOW);
  servosLigados = false;

  if (ALARME_FISICO_INSTALADO) {
    pinMode(J1.pinoAlarme, INPUT);
    pinMode(J2.pinoAlarme, INPUT);
  }

  engine.init();
  J1.motor = engine.stepperConnectToPin(J1.pinoPulso);
  J2.motor = engine.stepperConnectToPin(J2.pinoPulso);

  if (!J1.motor || !J2.motor) {
    Serial.println("!!! Falha ao conectar os geradores de pulso !!!");
    return false;
  }

  // A lembranca do que ja esta programado nao vale depois de reconectar
  // os geradores: no boot eles estao no padrao deles, nao no ultimo valor
  // que este firmware escreveu.
  velProgramada[0] = velProgramada[1] = 0;

  aplicarSentido();
  aplicarVelocidadeManual();
  aplicarAceleracao();
  return true;
}

// ---------------------------------------------------------------------
void servosHabilitar(bool ligar) {
  if (!ligar) {
    pararSuave();
    soldaDesligar();
  }
  digitalWrite(PIN_SERVO_ON, ligar ? HIGH : LOW);
  servosLigados = ligar;
  definirMensagem(ligar ? "Servos habilitados" : "Servos desabilitados (sem torque)");
}

bool motoresLerAlarmes() {
  // Sem a fiacao de ALM os pinos flutuam e leem ruido. Ler pino solto e
  // tratar como alarme trava o sistema inteiro, entao so lemos de fato
  // quando o hardware existe.
  if (!ALARME_FISICO_INSTALADO) {
    J1.alarme = false;
    J2.alarme = false;
    return false;
  }
  J1.alarme = (digitalRead(J1.pinoAlarme) == ALARME_ATIVO_EM);
  J2.alarme = (digitalRead(J2.pinoAlarme) == ALARME_ATIVO_EM);
  return J1.alarme || J2.alarme;
}

// ---------------------------------------------------------------------
uint32_t grausPorSegParaHz(const Junta& j, float grausPorS) {
  if (grausPorS <= 0.0f || j.passosPorGrau <= 0.0f) return 1;
  return limitarFreq((uint32_t)(grausPorS * j.passosPorGrau));
}

// Ultima velocidade REALMENTE programada em cada gerador.
//
// Existe porque seguirSetpoint() so reprograma quando o valor muda:
// chamar setSpeedInHz a cada milissegundo obriga o gerador a refazer a
// rampa o tempo todo. Mas o cache tem de ser o mesmo para TODO mundo que
// programa velocidade -- com um cache privado dentro de seguirSetpoint,
// um moverCoordenado() no meio deixava a lembranca velha, e o trecho
// seguinte podia rodar na velocidade do deslocamento sem reprogramar
// nada. Toda escrita de velocidade passa por aqui.

static void programarVelocidade(Junta& j, int i, uint32_t hz) {
  if (!j.motor) return;
  const uint32_t v = limitarFreq(hz);
  if (v == velProgramada[i]) return;
  velProgramada[i] = v;
  j.motor->setSpeedInHz(v);
}

// Cada junta recebe a MESMA velocidade angular, convertida com o seu
// proprio passosPorGrau. Antes as duas recebiam o mesmo Hz, e a de menor
// reducao andava varias vezes mais rapido.
void aplicarVelocidadeManual() {
  const float g = modoPrecisao ? velPrecisao : velNormal;
  programarVelocidade(J1, 0, grausPorSegParaHz(J1, g));
  programarVelocidade(J2, 1, grausPorSegParaHz(J2, g));
}

void aplicarAceleracao() {
  if (J1.motor) J1.motor->setAcceleration(grausPorSegParaHz(J1, J1.aceleracao));
  if (J2.motor) J2.motor->setAcceleration(grausPorSegParaHz(J2, J2.aceleracao));
  aplicarSuavidade();
}

// Sobe a aceleracao gradualmente nos primeiros passos em vez de aplicar
// o valor cheio de uma vez. E o degrau de torque da partida que se sente
// como tranco; alongar esse degrau tira o solavanco sem deixar o
// movimento mais lento.
void aplicarSuavidade() {
#if RAMPA_SUAVE_DISPONIVEL
  if (J1.motor) J1.motor->setLinearAcceleration(suavidadePartida);
  if (J2.motor) J2.motor->setLinearAcceleration(suavidadePartida);
#endif
}

// O segundo parametro do FastAccelStepper diz se o nivel ALTO no DIR faz
// a contagem SUBIR. Invertendo aqui, o contador e o braco passam a andar
// para o mesmo lado sem precisar trocar fio no driver.
void aplicarSentido() {
  if (J1.motor) J1.motor->setDirectionPin(J1.pinoDir, !J1.inverterDir);
  if (J2.motor) J2.motor->setDirectionPin(J2.pinoDir, !J2.inverterDir);
}

bool motoresEmMovimento() {
  return (J1.motor && J1.motor->isRunning()) ||
         (J2.motor && J2.motor->isRunning());
}

float velocidadeJ1Hz() {
  return J1.motor ? fabsf((float)J1.motor->getCurrentSpeedInMilliHz()) / 1000.0f : 0.0f;
}
float velocidadeJ2Hz() {
  return J2.motor ? fabsf((float)J2.motor->getCurrentSpeedInMilliHz()) / 1000.0f : 0.0f;
}

long posicaoJ1() { return J1.motor ? J1.motor->getCurrentPosition() : 0; }
long posicaoJ2() { return J2.motor ? J2.motor->getCurrentPosition() : 0; }

void zerarPosicoes() {
  if (J1.motor) J1.motor->setCurrentPosition(0);
  if (J2.motor) J2.motor->setCurrentPosition(0);
}

// ---------------------------------------------------------------------
// JOG
// ---------------------------------------------------------------------
void jogDefinir(uint8_t junta, int8_t direcao, float fracao) {
  if (junta != 1 && junta != 2) return;
  const uint8_t i = junta - 1;
  if (fracao < 0.0f) fracao = 0.0f;
  if (fracao > 1.0f) fracao = 1.0f;
  jogDir[i]      = direcao;
  jogFracao[i]   = fracao;
  jogUltimoMs[i] = millis();
}

void jogZerar() {
  jogDir[0] = jogDir[1] = 0;
  jogFracao[0] = jogFracao[1] = 1.0f;
  jogHzAplicado[0] = jogHzAplicado[1] = 0;
  jogSentidoAplicado[0] = jogSentidoAplicado[1] = 0;
  if (J1.motor) J1.motor->stopMove();
  if (J2.motor) J2.motor->stopMove();
}

// Distancia necessaria para frear: v^2/(2a), com a velocidade REAL do
// motor neste instante.
//
// Usar a velocidade maxima aqui (como na v3) reservava a freada de
// velocidade cheia mesmo com o eixo parado, e o jog travava dezenas de
// graus antes do limite. Parado, a reserva e praticamente zero e da
// para encostar no limite passo a passo.
static long distanciaFreada(const Junta& j) {
  if (!j.motor) return 0;
  const int32_t mHz = j.motor->getCurrentSpeedInMilliHz();
  const float v = fabsf((float)mHz) / 1000.0f;              // passos/s
  // A rampa e em graus/s2; a freada se calcula em passos.
  const float aGraus = (j.aceleracao > 0.0f) ? j.aceleracao : ACEL_PADRAO;
  const float a = aGraus * ((j.passosPorGrau > 0.0f) ? j.passosPorGrau : 1.0f);
  if (a <= 0.0f) return 4;
  return (long)(v * v / (2.0f * a)) + 4;
}

void jogAtualizar() {
  const uint32_t agora = millis();

  // Portao unico de movimento (ver estado.h). Sem servos habilitados o
  // gerador de pulso continua contando passos com o eixo parado, e todo
  // limite de curso passa a apontar para o lugar errado.
  if (!movimentoLiberado) {
    if (jogDir[0] != 0 || jogDir[1] != 0) {
      jogZerar();
      definirMensagem("Jog bloqueado: %s",
                      !servosLigados ? "habilite os servos" : "intertravamento de seguranca");
    }
    return;
  }

  for (uint8_t i = 0; i < 2; i++) {
    Junta& j = (i == 0) ? J1 : J2;
    if (!j.motor) continue;

    // Heartbeat: se a interface parou de confirmar o jog, o eixo para.
    // Protege contra queda de Wi-Fi com o botao pressionado.
    if (jogDir[i] != 0 && (agora - jogUltimoMs[i] > TIMEOUT_JOG_MS)) {
      jogDir[i] = 0;
    }

    if (jogDir[i] == 0) {
      if (jogSentidoAplicado[i] != 0) {
        jogSentidoAplicado[i] = 0;
        jogHzAplicado[i]      = 0;
        j.motor->stopMove();
      }
      continue;
    }

    // Velocidade proporcional a intensidade do joystick, em GRAUS/s: as
    // duas juntas andam igual, independente da engrenagem de cada uma.
    //
    // So reprograma quando o valor muda de fato. Chamar setSpeedInHz e
    // runForward a cada milissegundo obriga o gerador a refazer a rampa
    // o tempo todo, o que suja o trem de pulsos.
    {
      const float base = modoPrecisao ? velPrecisao : velNormal;
      float f = jogFracao[i];
      if (f < JOY_FRACAO_MIN) f = JOY_FRACAO_MIN;
      const uint32_t hz = grausPorSegParaHz(j, base * f);
      if (hz != jogHzAplicado[i]) {
        jogHzAplicado[i] = hz;
        programarVelocidade(j, i, hz);
        jogSentidoAplicado[i] = 0;    // forca reemitir o sentido
      }
    }

    // Antecipa a postura no fim da freada e para antes de violar.
    const long freada = distanciaFreada(j) * jogDir[i];
    const long f1 = (i == 0) ? posicaoJ1() + freada : posicaoJ1();
    const long f2 = (i == 1) ? posicaoJ2() + freada : posicaoJ2();

    const char* motivo = nullptr;
    if (!posturaValidaPassos(f1, f2, &motivo)) {
      // O destino e invalido. Antes de bloquear, veja se a posicao ATUAL
      // ja e invalida: nesse caso bloquear tudo prenderia o braco fora
      // da area util sem nenhuma saida. Enquanto o movimento reduzir a
      // violacao, ele e liberado - e um jog de recuperacao.
      const float gAtual  = gravidadeViolacaoPassos(posicaoJ1(), posicaoJ2());
      const float gFuturo = gravidadeViolacaoPassos(f1, f2);

      // "Nao piorar" e o criterio, nao "melhorar": para tirar uma junta
      // de fora da area util muitas vezes e preciso mexer na outra
      // primeiro. Qualquer movimento que aumente a violacao continua
      // bloqueado.
      if (!(gAtual > 0.001f && gFuturo <= gAtual + 0.001f)) {
        j.motor->stopMove();
        jogDir[i] = 0;
        definirMensagem("Junta %u bloqueada: %s", (unsigned)(i + 1),
                        motivo ? motivo : "limite");
        continue;
      }
      definirMensagem("Junta %u fora da area util: voltando", (unsigned)(i + 1));
    }

    if (jogSentidoAplicado[i] != jogDir[i]) {
      jogSentidoAplicado[i] = jogDir[i];
      if (jogDir[i] > 0) j.motor->runForward();
      else               j.motor->runBackward();
    }
  }
}

// ---------------------------------------------------------------------
// MOVIMENTO COORDENADO
// ---------------------------------------------------------------------
void moverCoordenado(long alvo1, long alvo2, float grausPorS) {
  if (!J1.motor || !J2.motor) return;

  const long d1 = labs(alvo1 - posicaoJ1());
  const long d2 = labs(alvo2 - posicaoJ2());
  if (d1 == 0 && d2 == 0) return;

  // Quem manda no tempo do movimento e a junta que tem mais GRAUS a
  // percorrer, nao mais passos: com engrenagens diferentes, mais passos
  // pode ser menos angulo.
  const float g1 = (J1.passosPorGrau > 0.0f) ? d1 / J1.passosPorGrau : 0.0f;
  const float g2 = (J2.passosPorGrau > 0.0f) ? d2 / J2.passosPorGrau : 0.0f;
  const float gmax = (g1 > g2) ? g1 : g2;
  if (gmax <= 0.0f) return;

  const float vel = (grausPorS > 0.01f) ? grausPorS : 1.0f;
  const float segundos = gmax / vel;          // as duas chegam junto

  const uint32_t v1 = limitarFreq((uint32_t)(d1 / segundos) + 1);
  const uint32_t v2 = limitarFreq((uint32_t)(d2 / segundos) + 1);

  // Aceleracao escalada na mesma proporcao: as duas rampas comecam e
  // terminam juntas, entao o caminho fica reto no espaco das juntas.
  uint32_t a1 = grausPorSegParaHz(J1, J1.aceleracao * (g1 / gmax));
  uint32_t a2 = grausPorSegParaHz(J2, J2.aceleracao * (g2 / gmax));
  if (a1 < 100) a1 = 100;
  if (a2 < 100) a2 = 100;

  J1.motor->setAcceleration(a1);
  J2.motor->setAcceleration(a2);
  programarVelocidade(J1, 0, v1);
  programarVelocidade(J2, 1, v2);
  J1.motor->moveTo(alvo1);
  J2.motor->moveTo(alvo2);
}

void seguirSetpoint(long alvo1, long alvo2, uint32_t vel1, uint32_t vel2) {
  if (!J1.motor || !J2.motor) return;
  programarVelocidade(J1, 0, vel1);
  programarVelocidade(J2, 1, vel2);
  J1.motor->moveTo(alvo1);
  J2.motor->moveTo(alvo2);
}

// ---------------------------------------------------------------------
void pararSuave() {
  jogDir[0] = jogDir[1] = 0;
  if (J1.motor) J1.motor->stopMove();
  if (J2.motor) J2.motor->stopMove();
}

void pararEmergencia() {
  // Ordem importa: primeiro o arco, depois o movimento.
  soldaDesligar();
  pararSuave();
  aplicarAceleracao();
  aplicarVelocidadeManual();
  // FALHA nao se limpa com uma parada: quem rearma e CMD_SERVOS, depois
  // de confirmar que o alarme do driver sumiu.
  if (modoAtual != MODO_FALHA) modoAtual = MODO_MANUAL;
  definirMensagem("PARADA: movimento interrompido e solda desligada");
}
