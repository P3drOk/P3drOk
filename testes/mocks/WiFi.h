// Mock de WiFi.
//
// Fiel no que o firmware realmente usa: modo AP+STA, IP fixo do AP,
// tentativa de entrar numa rede com sucesso ou falha encenados pelo
// teste, e varredura ASSINCRONA (que e o ponto delicado -- a varredura
// sincrona bloquearia a tarefa web por segundos).
#pragma once
#include "Arduino.h"
#include <string>
#include <vector>

#define WIFI_OFF     0
#define WIFI_STA     1
#define WIFI_AP      2
#define WIFI_AP_STA  3

#define WL_IDLE_STATUS      0
#define WL_NO_SSID_AVAIL    1
#define WL_CONNECTED        3
#define WL_CONNECT_FAILED   4
#define WL_DISCONNECTED     6

#define WIFI_AUTH_OPEN 0

#define WIFI_SCAN_RUNNING (-1)
#define WIFI_SCAN_FAILED  (-2)

struct RedeMock {
  std::string ssid;
  int32_t     rssi;
  int32_t     canal;
  uint8_t     cripto;
};

struct WiFiMock {
  // ---- encenacao do teste ----
  std::vector<RedeMock> vizinhanca;
  std::string senhaCerta = "certa";
  bool        falharAp   = false;
  uint32_t    msParaConectar = 500;   // quanto demora para entrar na rede
  uint32_t    msParaVarrer   = 2000;

  // ---- estado ----
  int         modoAtual = WIFI_OFF;
  std::string apSsid, apSenha, staSsid, staSenha, host;
  bool        apIpFixo = false;
  uint32_t    tentouEm = 0;
  bool        tentando = false;
  int         estado   = WL_IDLE_STATUS;
  uint32_t    varreuEm = 0;
  bool        varrendo = false;
  int         varreduraN = WIFI_SCAN_FAILED;

  void mode(int m) { modoAtual = m; }
  bool softAP(const char* s, const char* p) {
    apSsid = s ? s : ""; apSenha = p ? p : "";
    return !falharAp;
  }
  bool softAPConfig(uint32_t, uint32_t, uint32_t) { apIpFixo = true; return true; }
  const char* softAPIP() { return "192.168.4.1"; }
  void setHostname(const char* h) { host = h ? h : ""; }
  void setSleep(bool) {}
  void persistent(bool) {}
  void setAutoReconnect(bool) {}

  void begin(const char* s, const char* p) {
    staSsid = s ? s : ""; staSenha = p ? p : "";
    tentouEm = millis(); tentando = true; estado = WL_IDLE_STATUS;
  }
  void disconnect(bool = false, bool = false) {
    tentando = false; estado = WL_DISCONNECTED; staSsid.clear();
  }
  int status() {
    if (tentando && millis() - tentouEm >= msParaConectar) {
      tentando = false;
      bool existe = false;
      for (const RedeMock& r : vizinhanca) if (r.ssid == staSsid) existe = true;
      estado = (!existe)                  ? WL_NO_SSID_AVAIL
             : (staSenha != senhaCerta)   ? WL_CONNECT_FAILED
                                          : WL_CONNECTED;
    }
    return estado;
  }
  const char* localIP()  { return estado == WL_CONNECTED ? "192.168.0.77" : "0.0.0.0"; }
  int32_t     RSSI()     { return -52; }
  const char* SSID()     { return staSsid.c_str(); }

  int scanNetworks(bool assincrona = false, bool = false, bool = false, uint32_t = 300) {
    varreuEm = millis(); varrendo = true; varreduraN = WIFI_SCAN_RUNNING;
    if (!assincrona) { varrendo = false; varreduraN = (int)vizinhanca.size(); }
    return varreduraN;
  }
  int scanComplete() {
    if (varrendo && millis() - varreuEm >= msParaVarrer) {
      varrendo = false; varreduraN = (int)vizinhanca.size();
    }
    return varreduraN;
  }
  void scanDelete() { varreduraN = WIFI_SCAN_FAILED; varrendo = false; }

  const char* SSID(int i) { return vizinhanca[(size_t)i].ssid.c_str(); }
  int32_t     RSSI(int i) { return vizinhanca[(size_t)i].rssi; }
  int32_t     channel(int i) { return vizinhanca[(size_t)i].canal; }
  uint8_t     encryptionType(int i) { return vizinhanca[(size_t)i].cripto; }
};
extern WiFiMock WiFi;
