// Mock do DabbleESP32: um gamepad controlavel pelo banco de testes.
// Reproduz a API que controle_bt.cpp usa, incluindo o analogico em raio
// 0..7 e angulo em graus, que e como o aplicativo entrega.
#pragma once
#include "Arduino.h"

struct DabbleMock {
  bool conectado = false;
  bool cima=false, baixo=false, esq=false, dir=false;
  bool quadrado=false, circulo=false, cruz=false, triangulo=false;
  bool start=false, select=false;
  uint8_t raio = 0;      // 0..7
  int16_t angulo = 0;    // graus

  void soltarTudo() {
    cima=baixo=esq=dir=false;
    quadrado=circulo=cruz=triangulo=start=select=false;
    raio=0; angulo=0;
  }
};
extern DabbleMock g_dabble;

class GamePadMock {
 public:
  bool isUpPressed()       { return g_dabble.cima; }
  bool isDownPressed()     { return g_dabble.baixo; }
  bool isLeftPressed()     { return g_dabble.esq; }
  bool isRightPressed()    { return g_dabble.dir; }
  bool isSquarePressed()   { return g_dabble.quadrado; }
  bool isCirclePressed()   { return g_dabble.circulo; }
  bool isCrossPressed()    { return g_dabble.cruz; }
  bool isTrianglePressed() { return g_dabble.triangulo; }
  bool isStartPressed()    { return g_dabble.start; }
  bool isSelectPressed()   { return g_dabble.select; }
  uint8_t getRadius()      { return g_dabble.raio; }
  int16_t getAngle()       { return g_dabble.angulo; }
};

class DabbleClass {
 public:
  void begin(const char*) {}
  void processInput() {}
  bool isAppConnected() { return g_dabble.conectado; }
};

extern DabbleClass Dabble;
extern GamePadMock GamePad;
