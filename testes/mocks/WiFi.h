// Mock de WiFi.
//
// Fiel no que o firmware realmente usa: modo AP+STA, IP fixo do AP,
// tentativa de entrar numa rede com sucesso ou falha encenados pelo
// teste, e varredura ASSINCRONA (que e o ponto delicado -- a varredura
// sincrona bloquearia a tarefa web por segundos).
//
// AS ASSINATURAS SAO AS DO CORE DE VERDADE, nao as convenientes. Este
// arquivo ja devolveu const char* onde o ESP32 devolve String e
// IPAddress; o banco compilou limpo e a IDE do operador nao. Um mock que
// aceita mais que o original nao e um mock, e uma armadilha:
//
//   SSID(i)    -> String     (por valor; c_str() de temporario e cilada)
//   SSID()     -> String
//   localIP()  -> IPAddress  (NAO converte para const char*)
//   softAPIP() -> IPAddress
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

  bool mode(int m) { modoAtual = m; return true; }  // core: bool mode(wifi_mode_t)
  bool softAP(const char* s, const char* p) {   // core: bool softAP(const char*, const char*)
    apSsid = s ? s : ""; apSenha = p ? p : "";
    return !falharAp;
  }
  bool softAPConfig(IPAddress, IPAddress, IPAddress) { apIpFixo = true; return true; }
  IPAddress softAPIP() { return IPAddress(192, 168, 4, 1); }
  bool setHostname(const char* h) { host = h ? h : ""; return true; }   // core: bool
  bool setSleep(bool) { return true; }        // core: bool
  void persistent(bool) {}
  bool setAutoReconnect(bool) { return true; }// core: bool

  int begin(const char* s, const char* p) {   // core: wl_status_t
    staSsid = s ? s : ""; staSenha = p ? p : "";
    tentouEm = millis(); tentando = true; estado = WL_IDLE_STATUS;
    return estado;
  }
  bool disconnect(bool = false, bool = false) {   // core: bool
    tentando = false; estado = WL_DISCONNECTED; staSsid.clear();
    return true;
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
  IPAddress localIP() {
    return estado == WL_CONNECTED ? IPAddress(192, 168, 0, 77) : IPAddress(0, 0, 0, 0);
  }
  int8_t      RSSI()     { return -52; }   // core: int8_t na estacao
  String      SSID()     { return String(staSsid); }

  int16_t scanNetworks(bool assincrona = false, bool = false, bool = false, uint32_t = 300) {
    varreuEm = millis(); varrendo = true; varreduraN = WIFI_SCAN_RUNNING;
    if (!assincrona) { varrendo = false; varreduraN = (int)vizinhanca.size(); }
    return (int16_t)varreduraN;
  }
  int16_t scanComplete() {   // core: int16_t
    if (varrendo && millis() - varreuEm >= msParaVarrer) {
      varrendo = false; varreduraN = (int)vizinhanca.size();
    }
    return (int16_t)varreduraN;
  }
  void scanDelete() { varreduraN = WIFI_SCAN_FAILED; varrendo = false; }

  String      SSID(int i) { return String(vizinhanca[(size_t)i].ssid); }
  int32_t     RSSI(int i) { return vizinhanca[(size_t)i].rssi; }
  int32_t     channel(int i) { return vizinhanca[(size_t)i].canal; }
  uint8_t     encryptionType(int i) { return vizinhanca[(size_t)i].cripto; }
};
extern WiFiMock WiFi;
