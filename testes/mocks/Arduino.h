// Mock minimo de Arduino.h para rodar o firmware RoboCNC no PC.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cmath>
#include <string>
#include <map>
#include <cstdlib>

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
extern int  g_subidas[64];   // bordas de subida por pino
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
// No Arduino constrain e macro, entao aceita tipos mistos. Igual aqui.
#define constrain(v, lo, hi) ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))

// String do Arduino: o suficiente para o codigo do projeto compilar.
struct String : std::string {
  String() {}
  String(const char* s) : std::string(s ? s : "") {}
  String(const std::string& s) : std::string(s) {}
  String(int v)  { char b[24]; snprintf(b, sizeof(b), "%d", v);  assign(b); }
  String(float v, int casas) {
    char b[32]; snprintf(b, sizeof(b), "%.*f", casas, (double)v); assign(b);
  }
  String(double v, int casas) {
    char b[32]; snprintf(b, sizeof(b), "%.*f", casas, v); assign(b);
  }
  float toFloat() const { return empty() ? 0.0f : (float)atof(c_str()); }
  long  toInt()   const { return empty() ? 0L   : atol(c_str()); }
};
