// Mock de FastAccelStepper com rampa trapezoidal simulada.
// Fiel no que importa para os testes: isRunning(), posicao acumulada,
// velocidade instantanea e o fato de o contador de passos avancar
// INDEPENDENTEMENTE do sinal SON dos drivers.
#pragma once
#include "Arduino.h"

class FastAccelStepper {
 public:
  int32_t  pos = 0;
  int32_t  alvo = 0;
  uint32_t velHz = 1000;
  uint32_t acel  = 8000;
  int8_t   modo  = 0;        // 0=parado 1=posicionando 2=continuo
  int8_t   sentidoContinuo = 0;
  float    velAtual = 0.0f;  // Hz com sinal
  uint8_t  pinoDir = 0;

  void setDirectionPin(uint8_t p) { pinoDir = p; }
  void setSpeedInHz(uint32_t v)   { velHz = v ? v : 1; }
  void setAcceleration(uint32_t a){ acel = a ? a : 1; }
  uint32_t getAcceleration() const { return acel; }

  void moveTo(int32_t p) {
    alvo = p;
    modo = (p == pos && velAtual == 0.0f) ? 0 : 1;
  }
  void runForward()  { modo = 2; sentidoContinuo =  1; }
  void runBackward() { modo = 2; sentidoContinuo = -1; }
  void stopMove()    { if (modo != 0) modo = 3; }   // 3 = desacelerando

  bool isRunning() const { return modo != 0; }
  int32_t getCurrentPosition() const { return pos; }
  void setCurrentPosition(int32_t p) { pos = p; alvo = p; }
  int32_t getCurrentSpeedInMilliHz() const { return (int32_t)(velAtual * 1000.0f); }

  // Avanca a simulacao em dt milissegundos.
  void avancar(float dtMs) {
    const float dt = dtMs / 1000.0f;
    float alvoVel = 0.0f;

    if (modo == 1) {
      const float d = (float)(alvo - pos);
      if (fabsf(d) < 0.5f && fabsf(velAtual) < 1.0f) { modo = 0; velAtual = 0; return; }
      const float freada = velAtual * velAtual / (2.0f * (float)acel);
      alvoVel = (fabsf(d) <= freada) ? 0.0f : (d > 0 ? (float)velHz : -(float)velHz);
    } else if (modo == 2) {
      alvoVel = sentidoContinuo * (float)velHz;
    } else if (modo == 3) {
      alvoVel = 0.0f;
      if (fabsf(velAtual) < 1.0f) { velAtual = 0; modo = 0; return; }
    } else {
      velAtual = 0; return;
    }

    const float dv = (float)acel * dt;
    if (velAtual < alvoVel) velAtual = fminf(alvoVel, velAtual + dv);
    else                    velAtual = fmaxf(alvoVel, velAtual - dv);

    posFrac += velAtual * dt;
    const int32_t inteiro = (int32_t)posFrac;
    pos += inteiro;
    posFrac -= (float)inteiro;

    if (modo == 1 && ((velAtual >= 0 && pos >= alvo) || (velAtual <= 0 && pos <= alvo))) {
      pos = alvo; velAtual = 0; modo = 0;
    }
  }

 private:
  float posFrac = 0.0f;
};

class FastAccelStepperEngine {
 public:
  void init() {}
  // Um objeto por pino: chamar init()/stepperConnectToPin() de novo
  // (reinicio do sistema no banco de testes) devolve o mesmo gerador.
  FastAccelStepper* stepperConnectToPin(uint8_t pino) {
    for (int i = 0; i < n; i++) if (pinos[i] == pino) return &motores[i];
    if (n >= 4) return nullptr;
    pinos[n] = pino;
    motores[n] = FastAccelStepper();
    return &motores[n++];
  }
  FastAccelStepper motores[4];
  uint8_t pinos[4] = {255, 255, 255, 255};
  int n = 0;
};
