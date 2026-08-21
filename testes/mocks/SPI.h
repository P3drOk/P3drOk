#pragma once
#include "Arduino.h"
#define HSPI 2
#define VSPI 3
class SPIClass {
 public:
  SPIClass(int = VSPI) {}
  void begin(int8_t = -1, int8_t = -1, int8_t = -1, int8_t = -1) {}
  void end() {}
};
