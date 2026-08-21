#include "motores.h"
#include "cinematica.h"
#include "solda.h"
#include <math.h>

static FastAccelStepperEngine engine = FastAccelStepperEngine();

static int8_t   jogDir[2]     = {0, 0};
static uint32_t jogUltimoMs[2] = {0, 0};
static float    jogFracao[2]  = {1.0f, 1.0f};

// ---------------------------------------------------------------------
static uint32_t limitarFreq(uint32_t v) {
  if (v < 1) return 1;
  if (v > FREQ_PULSO_MAX_HZ) return FREQ_PULSO_MAX_HZ;
  return v;
}

// ---------------------------------------------------------------------
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

  J1.motor->setDirectionPin(J1.pinoDir);
  J2.motor->setDirectionPin(J2.pinoDir);
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
void aplicarVelocidadeManual() {
  const uint32_t v = limitarFreq(modoPrecisao ? velPrecisao : velNormal);
  if (J1.motor) J1.motor->setSpeedInHz(v);
  if (J2.motor) J2.motor->setSpeedInHz(v);
}

void aplicarAceleracao() {
  if (J1.motor) J1.motor->setAcceleration(J1.aceleracao);
  if (J2.motor) J2.motor->setAcceleration(J2.aceleracao);
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
  const float v = fabsf((float)mHz) / 1000.0f;
  const float a = (float)(j.aceleracao > 0 ? j.aceleracao : ACEL_PADRAO);
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
      j.motor->stopMove();
      continue;
    }

    // Velocidade proporcional a intensidade do joystick. A base continua
    // sendo a velocidade configurada (normal ou precisao), entao o teto
    // nao muda: o disco so escolhe qual fracao dela usar.
    {
      const uint32_t base = modoPrecisao ? velPrecisao : velNormal;
      float f = jogFracao[i];
      if (f < JOY_FRACAO_MIN) f = JOY_FRACAO_MIN;
      j.motor->setSpeedInHz(limitarFreq((uint32_t)(base * f)));
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

    if (jogDir[i] > 0) j.motor->runForward();
    else               j.motor->runBackward();
  }
}

// ---------------------------------------------------------------------
// MOVIMENTO COORDENADO
// ---------------------------------------------------------------------
void moverCoordenado(long alvo1, long alvo2, uint32_t velJunta) {
  if (!J1.motor || !J2.motor) return;

  const long d1 = labs(alvo1 - posicaoJ1());
  const long d2 = labs(alvo2 - posicaoJ2());
  const long dmax = (d1 > d2) ? d1 : d2;
  if (dmax == 0) return;

  const uint32_t vBase = limitarFreq(velJunta);

  const uint32_t v1 = limitarFreq((uint32_t)((uint64_t)vBase * d1 / dmax));
  const uint32_t v2 = limitarFreq((uint32_t)((uint64_t)vBase * d2 / dmax));

  // Aceleracao escalada na mesma proporcao: as duas rampas comecam e
  // terminam juntas, entao o caminho fica reto no espaco das juntas.
  uint32_t a1 = (uint32_t)((uint64_t)J1.aceleracao * d1 / dmax);
  uint32_t a2 = (uint32_t)((uint64_t)J2.aceleracao * d2 / dmax);
  if (a1 < 100) a1 = 100;
  if (a2 < 100) a2 = 100;

  J1.motor->setAcceleration(a1);
  J2.motor->setAcceleration(a2);
  J1.motor->setSpeedInHz(v1);
  J2.motor->setSpeedInHz(v2);
  J1.motor->moveTo(alvo1);
  J2.motor->moveTo(alvo2);
}

void seguirSetpoint(long alvo1, long alvo2, uint32_t vel1, uint32_t vel2) {
  if (!J1.motor || !J2.motor) return;
  J1.motor->setSpeedInHz(limitarFreq(vel1));
  J2.motor->setSpeedInHz(limitarFreq(vel2));
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
