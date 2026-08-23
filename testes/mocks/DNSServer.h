// Mock do DNS de captura. Guarda o que foi anunciado para o banco
// conferir; nao existe socket aqui.
#pragma once
#include "Arduino.h"
#include <string>

class DNSServer {
 public:
  // O firmware guarda o servidor num static dentro de rede.cpp. O banco
  // alcanca por aqui, mesmo padrao do mock de WebServer.
  DNSServer() { atual = this; }
  static DNSServer* atual;

  bool     ligado = false;
  uint16_t porta  = 0;
  std::string dominio;
  uint32_t pedidos = 0;

  bool start(uint16_t p, const String& d, const IPAddress&) {
    porta = p; dominio = d; ligado = true; return true;
  }
  void processNextRequest() { if (ligado) pedidos++; }
  void stop() { ligado = false; }
};

// Atalho do banco: o servidor de DNS que o firmware criou.
#define g_dns (*DNSServer::atual)
