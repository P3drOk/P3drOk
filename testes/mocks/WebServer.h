// Mock de WebServer suficiente para conferir a compilacao de
// servidor_web.cpp. O arquivo nao entra no banco de testes (roda no core
// 0 e depende de rede), mas precisa ao menos compilar.
#pragma once
#include "Arduino.h"

#define HTTP_GET  1
#define HTTP_POST 3

class WebServer {
 public:
  WebServer(int) {}
  void on(const char*, int, void (*)()) {}
  void onNotFound(void (*)()) {}
  void begin() {}
  void handleClient() {}
  void send(int, const char*, const String&) {}
  void send(int, const char*, const char*) {}
  void send_P(int, const char*, const char*) {}
  bool hasArg(const char*) { return false; }
  String arg(const char*) { return String(); }
  String uri() { return String(); }
};

// String do Arduino tem toFloat()/toInt() e String(valor, casas); o
// std::string do mock nao. Estes helpers cobrem o uso de servidor_web.cpp.
inline float  toFloatArg(const String& s) { return s.empty() ? 0.0f : atof(s.c_str()); }
inline long   toIntArg  (const String& s) { return s.empty() ? 0L   : atol(s.c_str()); }
