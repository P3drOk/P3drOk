// Mock de mDNS: guarda o nome anunciado para o banco conferir.
#pragma once
#include "Arduino.h"
#include <string>

struct MDNSMock {
  std::string nome;
  bool ativo = false;
  bool begin(const char* n) { nome = n ? n : ""; ativo = true; return true; }
  void end() { ativo = false; }
  void addService(const char*, const char*, uint16_t) {}
};
extern MDNSMock MDNS;
