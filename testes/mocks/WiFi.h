#pragma once
#include "Arduino.h"
#define WIFI_AP 2
struct IPMock { };
inline void print_ip() {}
struct WiFiMock {
  void mode(int) {}
  bool softAP(const char*, const char*) { return true; }
  const char* softAPIP() { return "192.168.4.1"; }
};
extern WiFiMock WiFi;
