// Mock de NVS: um mapa em RAM que sobrevive entre begin()/end().
#pragma once
#include "Arduino.h"
#include <map>
#include <string>

struct NvsMock {
  std::map<std::string, uint32_t> u;
  std::map<std::string, long>     l;
  std::map<std::string, float>    f;
  std::map<std::string, bool>     b;
  std::map<std::string, std::string> s;
};
extern NvsMock g_nvs;

class Preferences {
 public:
  bool begin(const char*, bool) { return true; }
  void end() {}
  uint32_t getUInt (const char* k, uint32_t d) { auto i=g_nvs.u.find(k); return i==g_nvs.u.end()?d:i->second; }
  long     getLong (const char* k, long d)     { auto i=g_nvs.l.find(k); return i==g_nvs.l.end()?d:i->second; }
  float    getFloat(const char* k, float d)    { auto i=g_nvs.f.find(k); return i==g_nvs.f.end()?d:i->second; }
  bool     getBool (const char* k, bool d)     { auto i=g_nvs.b.find(k); return i==g_nvs.b.end()?d:i->second; }
  void putUInt (const char* k, uint32_t v) { g_nvs.u[k]=v; }
  void putLong (const char* k, long v)     { g_nvs.l[k]=v; }
  void putFloat(const char* k, float v)    { g_nvs.f[k]=v; }
  void putBool (const char* k, bool v)     { g_nvs.b[k]=v; }

  // As credenciais de Wi-Fi sao texto: o NVS de verdade guarda string, e
  // sem isto o banco nao consegue exercitar o caminho de gravar rede.
  size_t getString(const char* k, char* destino, size_t tam) {
    auto i = g_nvs.s.find(k);
    if (destino && tam) destino[0] = '\0';
    if (i == g_nvs.s.end() || !destino || !tam) return 0;
    const size_t n = i->second.size() < tam - 1 ? i->second.size() : tam - 1;
    memcpy(destino, i->second.data(), n);
    destino[n] = '\0';
    return n;
  }
  void putString(const char* k, const char* v) { g_nvs.s[k] = v ? v : ""; }
};
