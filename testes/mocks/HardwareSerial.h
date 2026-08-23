// Mock de HardwareSerial COM um escravo Modbus dentro.
//
// Nao e so um cano mudo: ele entende o quadro que o firmware manda e
// responde como um driver responderia. Assim o banco exercita o mestre
// Modbus de verdade -- CRC, ordem das palavras, excecao, silencio --
// sem hardware nenhum.
//
// Assinaturas iguais as do core do ESP32. Ver testes/mocks/LEIA-ME.md.
#pragma once
#include "Arduino.h"
#include <deque>
#include <vector>

#ifndef SERIAL_8N1
#define SERIAL_8N1 0x800001cu
#define SERIAL_8E1 0x800001eu
#define SERIAL_8O1 0x800001fu
#endif

struct EscravoModbus {
  bool     existe   = true;
  uint8_t  id       = 1;
  uint8_t  funcao   = 3;
  uint16_t regBase  = 0x1000;   // onde a posicao mora
  int32_t  posicao  = 0;        // contagem do encoder
  bool     baixaPrimeiro = false;
  bool     mudo     = false;    // encena driver que nao responde
  uint8_t  excecao  = 0;        // se != 0, responde excecao
  bool     crcRuim  = false;
  // Driver que so aceita UM registrador por pergunta. Existe de verdade:
  // o programa de bancada nunca pediu dois de uma vez, entao ninguem
  // provou que o T3D aceita. Com isto o banco exercita esse driver.
  bool     soUmRegistrador = false;
  uint32_t perguntas = 0;
};

class HardwareSerial {
 public:
  explicit HardwareSerial(int) { atual = this; }

  // core: void begin(unsigned long, uint32_t, int8_t, int8_t, ...)
  void begin(unsigned long baud, uint32_t cfg = SERIAL_8N1,
             int8_t rxPin = -1, int8_t txPin = -1) {
    baudAtual = baud; cfgAtual = cfg; pinRx = rxPin; pinTx = txPin;
    aberta = true; fila.clear();
  }
  void end() { aberta = false; fila.clear(); }

  int available() { return (int)fila.size(); }
  int read() {
    if (fila.empty()) return -1;
    const int c = fila.front(); fila.pop_front(); return c;
  }
  size_t write(const uint8_t* b, size_t n) { responder(b, n); return n; }
  size_t write(uint8_t b) { return write(&b, 1); }
  void flush() {}

  // ---- estado que o banco inspeciona ----
  unsigned long baudAtual = 0;
  uint32_t      cfgAtual  = 0;
  int8_t        pinRx = -1, pinTx = -1;
  bool          aberta = false;
  EscravoModbus escravo[2];

  static HardwareSerial* atual;

 private:
  std::deque<uint8_t> fila;

  static uint16_t crc16(const uint8_t* b, size_t n) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
      crc ^= b[i];
      for (uint8_t k = 0; k < 8; k++)
        crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0xA001) : (uint16_t)(crc >> 1);
    }
    return crc;
  }
  void empurrar(std::vector<uint8_t>& r, bool comCrc) {
    if (comCrc) {
      const uint16_t c = crc16(r.data(), r.size());
      r.push_back((uint8_t)(c & 0xFF));
      r.push_back((uint8_t)(c >> 8));
    }
    for (uint8_t b : r) fila.push_back(b);
  }

  void responder(const uint8_t* q, size_t n) {
    if (n != 8) return;
    const uint16_t c = crc16(q, 6);
    if (q[6] != (uint8_t)(c & 0xFF) || q[7] != (uint8_t)(c >> 8)) return;

    for (EscravoModbus& e : escravo) {
      if (!e.existe || e.id != q[0]) continue;
      e.perguntas++;
      if (e.mudo) return;

      std::vector<uint8_t> r;
      if (e.excecao) {
        r = {e.id, (uint8_t)(q[1] | 0x80), e.excecao};
        empurrar(r, !e.crcRuim);
        if (e.crcRuim) { fila.push_back(0); fila.push_back(0); }
        return;
      }
      if (q[1] != e.funcao) {
        r = {e.id, (uint8_t)(q[1] | 0x80), 1};   // funcao ilegal
        empurrar(r, true);
        return;
      }
      const uint16_t reg = (uint16_t)((q[2] << 8) | q[3]);
      const uint16_t qtd = (uint16_t)((q[4] << 8) | q[5]);

      // A posicao mora em regBase (primeira palavra) e regBase+1
      // (segunda), na ordem que baixaPrimeiro diz. Ler uma so, ou as
      // duas de uma vez, tem que dar a mesma coisa -- e por isso o
      // escravo do banco e uma tabela de registradores de verdade, e nao
      // um caso especial para cada pergunta.
      const uint16_t alta  = (uint16_t)(((uint32_t)e.posicao >> 16) & 0xFFFF);
      const uint16_t baixa = (uint16_t)((uint32_t)e.posicao & 0xFFFF);
      const uint16_t palavra[2] = { e.baixaPrimeiro ? baixa : alta,
                                    e.baixaPrimeiro ? alta  : baixa };

      if (e.soUmRegistrador && qtd > 1) {
        r = {e.id, (uint8_t)(q[1] | 0x80), 3};   // valor de dado ilegal
        empurrar(r, true);
        return;
      }
      if (qtd < 1 || qtd > 2 || reg < e.regBase || reg + qtd > e.regBase + 2) {
        r = {e.id, (uint8_t)(q[1] | 0x80), 2};   // endereco ilegal
        empurrar(r, true);
        return;
      }

      r = {e.id, e.funcao, (uint8_t)(qtd * 2)};
      for (uint16_t k = 0; k < qtd; k++) {
        const uint16_t v = palavra[(reg - e.regBase) + k];
        r.push_back((uint8_t)(v >> 8));
        r.push_back((uint8_t)(v & 0xFF));
      }
      empurrar(r, !e.crcRuim);
      if (e.crcRuim) { fila.push_back(0xAA); fila.push_back(0xBB); }
      return;
    }
  }
};

// Atalho do banco: a UART que o firmware criou.
#define g_uart (*HardwareSerial::atual)
