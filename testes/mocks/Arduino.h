// Mock minimo de Arduino.h para rodar o firmware RoboCNC no PC.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cmath>
#include <string>
#include <map>

#define PROGMEM
#define HIGH 1
#define LOW  0
#define INPUT        0
#define OUTPUT       1
#define INPUT_PULLUP 2

// ---- relogio simulado -------------------------------------------------
extern uint32_t g_millis;
inline uint32_t millis() { return g_millis; }
inline void delay(uint32_t ms) { g_millis += ms; }

// ---- GPIO simulado ----------------------------------------------------
extern int  g_pinModo[64];
extern int  g_pinSaida[64];
extern int  g_pinEntrada[64];
extern int  g_escritasRele;
void pinMode(uint8_t p, int m);
void digitalWrite(uint8_t p, int v);
int  digitalRead(uint8_t p);

// ---- Serial -----------------------------------------------------------
extern bool g_serialSilencioso;
extern uint32_t g_serialBytes;      // total transmitido (para medir bloqueio)
extern uint32_t g_msgCount;         // quantas definirMensagem() foram emitidas
struct SerialMock {
  void begin(uint32_t) {}
  void print(const char* s)   { if (!strcmp(s, "[MSG] ")) g_msgCount++; emitir("%s", s); }
  void print(int v)           { emitir("%d", v); }
  void println()              { emitir("\n"); }
  void println(const char* s) { emitir("%s\n", s); }
  void println(int v)         { emitir("%d\n", v); }
  void println(const std::string& s) { emitir("%s\n", s.c_str()); }
  void printf(const char* fmt, ...) {
    va_list a; va_start(a, fmt); vemitir(fmt, a); va_end(a);
  }
  void emitir(const char* fmt, ...) {
    va_list a; va_start(a, fmt); vemitir(fmt, a); va_end(a);
  }
  void vemitir(const char* fmt, va_list a) {
    char buf[512];
    int n = vsnprintf(buf, sizeof(buf), fmt, a);
    if (n > 0) g_serialBytes += (uint32_t)n;
    if (!g_serialSilencioso) fputs(buf, stdout);
  }
};
extern SerialMock Serial;

// ---- utilitarios ------------------------------------------------------
template <typename T> T constrain(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
using String = std::string;
