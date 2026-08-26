#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string>

// Mock do Update do ESP32. A assinatura e a do core, nao a conveniente:
// begin() devolve bool, write() devolve os bytes escritos, end(bool)
// devolve bool.
//
// O que ele encena e a POLITICA -- ha particao, o cabecalho e valido, o
// tamanho bate. A gravacao em flash de verdade nao tem como ser testada
// no PC, e fingir que tem seria pior que nao testar.
#define UPDATE_SIZE_UNKNOWN 0xFFFFFFFF

class UpdateMock {
 public:
  bool begin(uint32_t = UPDATE_SIZE_UNKNOWN) {
    if (semParticao) { erro = "no partition"; return false; }
    if (aberto)      { erro = "already running"; return false; }
    aberto = true; escrito = 0; erro = ""; return true;
  }
  size_t write(uint8_t* dados, size_t n) {
    if (!aberto) return 0;
    // Encena flash cheia: escreve so o que cabe, como o core faz.
    if (limite && escrito + n > limite) { erro = "not enough space"; return 0; }
    (void)dados; escrito += n; return n;
  }
  bool end(bool = false) {
    if (!aberto) { erro = "not running"; return false; }
    aberto = false;
    if (escrito < minimoValido) { erro = "truncated image"; return false; }
    gravado = escrito;
    return true;
  }
  void abort() { aberto = false; erro = "aborted"; }
  bool hasError() const { return !erro.empty(); }
  const char* errorString() const { return erro.c_str(); }

  // Controles do banco.
  bool     semParticao  = false;
  uint32_t limite       = 0;        // 0 = sem limite
  uint32_t minimoValido = 1;
  bool     aberto       = false;
  size_t   escrito      = 0;
  size_t   gravado      = 0;
  std::string erro;
};

extern UpdateMock Update;
