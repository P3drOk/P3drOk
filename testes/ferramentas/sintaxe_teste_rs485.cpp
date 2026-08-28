// =====================================================================
//  Conferencia de sintaxe dos sketches AVULSOS de ferramentas/
//
//  Eles nao entram no banco: nao ha o que simular num programa que
//  conversa com um driver de verdade pela serial. Mas sketch que nao
//  compila e pior que sketch que nao existe -- o operador so descobre na
//  bancada, com o robo aberto e a IDE reclamando.
//
//  Este arquivo troca os enfeites do Arduino por equivalentes de PC e
//  pede ao compilador para conferir a sintaxe. Nao roda nada.
// =====================================================================
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>

struct String : std::string {
  String() {}
  String(const char* s) : std::string(s) {}
  String(const std::string& s) : std::string(s) {}
  void   trim() {}
  int    indexOf(char c) const { const auto p = find(c); return p == npos ? -1 : (int)p; }
  String substring(int a) const { return String(std::string(substr(a))); }
  String substring(int a, int b) const { return String(std::string(substr(a, b - a))); }
};

enum { DEC = 10, HEX = 16 };

struct SerialT {
  void begin(long) {}
  void println() {}
  void println(const char*) {}
  void println(int, int = DEC) {}
  void println(unsigned, int = DEC) {}
  void println(long, int = DEC) {}
  void println(unsigned long, int = DEC) {}
  void println(double) {}
  void print(const char*) {}
  void print(int, int = DEC) {}
  void print(unsigned, int = DEC) {}
  void print(long, int = DEC) {}
  void print(unsigned long, int = DEC) {}
  void print(double) {}
  void printf(const char*, ...) {}
  int  available() { return 1; }
  int  read() { return -1; }
  String readStringUntil(char) { return String("S"); }
  void flush() {}
  size_t write(const uint8_t*, size_t n) { return n; }
};
static SerialT Serial;

#define SERIAL_8N1 0u
#define SERIAL_8E1 1u
#define SERIAL_8O1 2u
#define OUTPUT 1
#define INPUT  0
#define INPUT_PULLUP 2
#define HIGH   1
#define LOW    0

struct HardwareSerial {
  HardwareSerial(int) {}
  void   begin(unsigned long, uint32_t, int, int) {}
  void   end() {}
  int    available() { return 0; }
  int    read() { return -1; }
  size_t write(const uint8_t*, size_t n) { return n; }
  void   flush() {}
};

static void pinMode(int, int) {}
static void digitalWrite(int, int) {}
static int  digitalRead(int) { return 0; }
static unsigned long millis() { return 0; }
static unsigned long micros() { return 0; }
static void delay(unsigned long) {}
static void delayMicroseconds(unsigned long) {}

#include "../../ferramentas/teste_rs485/teste_rs485.ino"

int main() { setup(); loop(); return 0; }
