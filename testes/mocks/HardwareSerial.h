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
#include "Arduino.h"
#include <deque>
#include <vector>
#include <map>

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
  int32_t  posicao  = 0;        // contagem do encoder no instante posicaoT0
  // Um eixo de verdade gira ENTRE as leituras, nao em degraus quando o
  // teste manda. Sem isto, tres leituras dentro do mesmo passo dao delta
  // zero e velocidade zero -- e o banco reprovaria um calculo correto.
  int32_t  velocidade = 0;      // contagens por segundo (0 = parado)
  uint32_t posicaoT0  = 0;      // instante a que 'posicao' se refere
  bool     baixaPrimeiro = false;
  bool     mudo     = false;    // encena driver que nao responde
  uint8_t  excecao  = 0;        // se != 0, responde excecao
  bool     crcRuim  = false;
  // Driver que so aceita UM registrador por pergunta. Existe de verdade:
  // o programa de bancada nunca pediu dois de uma vez, entao ninguem
  // provou que o T3D aceita. Com isto o banco exercita esse driver.
  bool     soUmRegistrador = false;
  // Um registrador que mexe sem ser posicao: erro de seguimento,
  // velocidade. Existe de verdade e engana a cacada -- na maquina do
  // operador o 94 ia de 0 para 65535 (que com sinal e -1) e o 95 pulava
  // para os dois lados. Sem isto no mock, o crivo do sentido nao teria
  // como ser testado.
  uint16_t ruidoReg   = 0xFFFF;   // 0xFFFF = nenhum
  uint16_t ruidoValor = 0;

  // ---- ESCRITA (o habilita mora aqui desde que o SON deixou de ser fio)
  //
  // O escravo guarda o que foi escrito e devolve na releitura, que e
  // como um driver de verdade se comporta -- e e a releitura que o
  // firmware usa como prova. Um mock que aceitasse a escrita e
  // continuasse devolvendo o valor velho reprovaria codigo correto; um
  // que devolvesse o valor novo sem guardar nada nao pegaria o driver
  // que mente. Por isso a tabela e de verdade.
  std::map<uint16_t, uint16_t> escritos;
  bool     recusaEscrita = false;  // encena driver so-leitura (excecao 2)
  bool     escritaMuda   = false;  // aceita, responde, e NAO guarda
  bool     soFuncao16    = false;  // recusa a 06, como ha driver que faz
  uint32_t escritas      = 0;      // quantas chegaram, para o banco contar
  uint32_t perguntas = 0;

  // Onde o eixo esta AGORA, andando desde posicaoT0 na velocidade dada.
  int32_t posicaoAgora() const {
    if (!velocidade) return posicao;
    const int32_t dt = (int32_t)(g_millis - posicaoT0);
    return posicao + (int32_t)((int64_t)velocidade * dt / 1000);
  }
  // Poe o eixo a girar a partir de onde ele esta neste instante.
  void girar(int32_t contagensPorSegundo) {
    posicao    = posicaoAgora();
    posicaoT0  = g_millis;
    velocidade = contagensPorSegundo;
  }
  void parar() { girar(0); }
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
  size_t write(const uint8_t* b, size_t n) {
    if (ecoAgora()) { for (size_t i = 0; i < n; i++) fila.push_back(b[i]); }
    responder(b, n);
    return n;
  }
  size_t write(uint8_t b) { return write(&b, 1); }
  void flush() {}

  // ---- estado que o banco inspeciona ----
  unsigned long baudAtual = 0;
  uint32_t      cfgAtual  = 0;
  int8_t        pinRx = -1, pinTx = -1;
  bool          aberta = false;
  EscravoModbus escravo[2];
  // O MAX485 devolve pelo RO o que sai pelo DI sempre que o receptor
  // estiver ligado durante a transmissao -- e o receptor esta ligado
  // quando o RE esta em BAIXO. Nao e um botao do banco: e consequencia
  // do que o firmware faz com o pino, e por isso o mock olha o pino.
  //
  // A excecao e o modo RS485 meio-duplex por hardware: ali o proprio
  // periferico desliga a recepcao enquanto transmite, e nao ha eco por
  // mais que o RE esteja em baixo.
  bool moduloLigado = false;   // ha um MAX485 no barramento
  int  pinoRe       = -1;      // qual pino e o RE (o banco informa)

  static HardwareSerial* atual;

 private:
  std::deque<uint8_t> fila;

  // Valor parado de um endereco que nao e a posicao. Precisa ser sempre
  // o mesmo para o mesmo endereco: a cacada compara duas leituras, e um
  // valor que mudasse sozinho apareceria como se fosse o encoder.
  static uint16_t parametro(uint16_t a) {
    return (uint16_t)(((uint32_t)a * 2654435761u) >> 20) & 0x3FFF;
  }

  bool ecoAgora() const {
    if (!moduloLigado || pinoRe < 0 || pinoRe >= 64) return false;
    return g_pinSaida[pinoRe] == 0;      // RE em baixo = recebendo
  }

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

  // Escrita: funcao 06 (8 bytes) e funcao 16 de um registrador (11).
  // Devolve true se tratou o quadro -- a leitura nem chega a ser tentada.
  bool responderEscrita(const uint8_t* q, size_t n) {
    const bool f06 = (n == 8  && q[1] == 6);
    const bool f16 = (n == 11 && q[1] == 16);
    if (!f06 && !f16) return false;

    const size_t corpo = n - 2;
    const uint16_t c = crc16(q, corpo);
    if (q[corpo] != (uint8_t)(c & 0xFF) || q[corpo + 1] != (uint8_t)(c >> 8)) return true;

    for (EscravoModbus& e : escravo) {
      if (!e.existe || e.id != q[0]) continue;
      e.perguntas++;
      e.escritas++;
      if (e.mudo) return true;

      std::vector<uint8_t> r;
      if (e.recusaEscrita || (e.soFuncao16 && f06)) {
        r = {e.id, (uint8_t)(q[1] | 0x80), 2};   // endereco de dado ilegal
        empurrar(r, true);
        return true;
      }

      const uint16_t reg = (uint16_t)((q[2] << 8) | q[3]);
      const uint16_t val = f06 ? (uint16_t)((q[4] << 8) | q[5])
                               : (uint16_t)((q[7] << 8) | q[8]);
      // O driver que MENTE: responde "aceitei" e guarda o valor velho. E
      // exatamente por ele que o firmware confere relendo, entao o banco
      // precisa conseguir encena-lo.
      if (!e.escritaMuda) e.escritos[reg] = val;

      // As duas funcoes ecoam o cabecalho: id, funcao, endereco, e
      // depois o valor (06) ou a quantidade (16).
      r = {e.id, q[1], q[2], q[3]};
      if (f06) { r.push_back(q[4]); r.push_back(q[5]); }
      else     { r.push_back(0);    r.push_back(1);    }
      empurrar(r, true);
      return true;
    }
    return true;
  }

  void responder(const uint8_t* q, size_t n) {
    if (responderEscrita(q, n)) return;
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
      const int32_t  pos   = e.posicaoAgora();
      const uint16_t alta  = (uint16_t)(((uint32_t)pos >> 16) & 0xFFFF);
      const uint16_t baixa = (uint16_t)((uint32_t)pos & 0xFFFF);
      const uint16_t palavra[2] = { e.baixaPrimeiro ? baixa : alta,
                                    e.baixaPrimeiro ? alta  : baixa };

      if (e.soUmRegistrador && qtd > 1) {
        r = {e.id, (uint8_t)(q[1] | 0x80), 3};   // valor de dado ilegal
        empurrar(r, true);
        return;
      }
      if (qtd < 1 || qtd > 8) {
        r = {e.id, (uint8_t)(q[1] | 0x80), 3};
        empurrar(r, true);
        return;
      }

      // Um driver de verdade responde a TABELA INTEIRA, nao so aos dois
      // registradores que o firmware costuma pedir: no log da maquina do
      // operador os 256 enderecos responderam. Quase todos sao parametro
      // parado; so o par da posicao anda quando o eixo gira. E essa a
      // diferenca que a cacada procura, entao o mock precisa te-la.
      r = {e.id, e.funcao, (uint8_t)(qtd * 2)};
      for (uint16_t k = 0; k < qtd; k++) {
        const uint16_t a = (uint16_t)(reg + k);
        uint16_t v;
        const auto it = e.escritos.find(a);
        if (it != e.escritos.end())  v = it->second;
        else if (a == e.regBase)          v = palavra[0];
        else if (a == e.regBase + 1) v = palavra[1];
        else if (a == e.ruidoReg)    v = e.ruidoValor;
        else                         v = parametro(a);
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
