// Mock de WebServer.
//
// Nao e mais so um esqueleto de compilacao: ele registra as rotas de
// verdade e sabe despachar um pedido. servidor_web.cpp entra no banco de
// testes, porque ja foi ali que apareceram dois defeitos que o operador
// sentiu na mao (botao que nao fazia nada, velocidade de cordao que nao
// salvava) e que nenhum teste de motor pegaria.
//
// O que continua faltando de proposito: socket, concorrencia e HTTP de
// verdade. O banco chama o handler direto, na mesma thread.
#pragma once
#include "Arduino.h"
#include <map>
#include <vector>

#define HTTP_GET  1
#define HTTP_POST 3

class WebServer {
 public:
  struct Rota { std::string uri; int metodo; void (*fn)(); };

  explicit WebServer(int) { atual = this; }

  void on(const char* uri, int metodo, void (*fn)()) {
    rotas.push_back(Rota{uri ? uri : "", metodo, fn});
  }
  void onNotFound(void (*fn)()) { naoEncontrado = fn; }
  void begin() {}
  void handleClient() {}

  void send(int codigo, const char* tipo, const String& corpo) {
    respCodigo = codigo; respTipo = tipo ? tipo : ""; respCorpo = corpo;
  }
  void send(int codigo, const char* tipo, const char* corpo) {
    send(codigo, tipo, String(corpo ? corpo : ""));
  }
  void send_P(int codigo, const char* tipo, const char* corpo) {
    send(codigo, tipo, corpo);
  }
  void send_P(int codigo, const char* tipo, const char*, size_t n) {
    respCodigo = codigo; respTipo = tipo ? tipo : "";
    respCorpo = String(""); respBytes = n;
  }
  // O core guarda o cabecalho e manda junto com a resposta. Descartar
  // aqui deixaria um redirecionamento passar por "resposta vazia" no
  // banco, e o teste nao veria para onde o navegador foi mandado.
  void sendHeader(const char* nome, const char* valor, bool = false) {
    cabecalhos[nome] = valor ? valor : "";
  }
  const char* cabecalho(const char* nome) {
    auto it = cabecalhos.find(nome);
    return it == cabecalhos.end() ? "" : it->second.c_str();
  }
  std::map<std::string, std::string> cabecalhos;

  bool   hasArg(const char* nome) { return args.count(nome ? nome : "") > 0; }
  String arg(const char* nome) {
    auto it = args.find(nome ? nome : "");
    return it == args.end() ? String("") : String(it->second);
  }
  String uri() { return String(uriAtual); }

  // O WebServer do ESP32 decodifica percent-encoding e '+' antes de
  // entregar o argumento. Sem isto aqui, o banco veria "Oficina%202G" e
  // um defeito de decodificacao passaria batido -- a pagina manda tudo
  // por encodeURIComponent.
  static std::string decodificar(const std::string& e) {
    std::string r;
    for (size_t i = 0; i < e.size(); i++) {
      if (e[i] == '+') { r += ' '; continue; }
      if (e[i] == '%' && i + 2 < e.size()) {
        const std::string h = e.substr(i + 1, 2);
        char* fim = nullptr;
        const long v = strtol(h.c_str(), &fim, 16);
        if (fim && *fim == '\0') { r += (char)v; i += 2; continue; }
      }
      r += e[i];
    }
    return r;
  }

  // ---- lado do banco de testes -------------------------------------
  // Despacha "/api/x?a=1&b=2" com corpo opcional (vira o arg "plain",
  // como faz o WebServer do ESP32 quando o Content-Type nao e de
  // formulario). Devolve o codigo HTTP; 0 = rota inexistente.
  int pedir(int metodo, const std::string& alvo, const char* corpo = nullptr) {
    args.clear();
    cabecalhos.clear();
    respCodigo = 0; respCorpo = String(""); respBytes = 0;
    const size_t i = alvo.find('?');
    uriAtual = alvo.substr(0, i == std::string::npos ? alvo.size() : i);
    if (i != std::string::npos) {
      std::string q = alvo.substr(i + 1);
      size_t p = 0;
      while (p <= q.size()) {
        const size_t e = q.find('&', p);
        const std::string par = q.substr(p, e == std::string::npos ? std::string::npos : e - p);
        const size_t ig = par.find('=');
        if (!par.empty()) {
          if (ig == std::string::npos) args[decodificar(par)] = "";
          else args[decodificar(par.substr(0, ig))] = decodificar(par.substr(ig + 1));
        }
        if (e == std::string::npos) break;
        p = e + 1;
      }
    }
    if (corpo) args["plain"] = corpo;

    for (const Rota& r : rotas)
      if (r.uri == uriAtual && r.metodo == metodo) { r.fn(); return respCodigo; }
    if (naoEncontrado) { naoEncontrado(); return respCodigo; }
    return 0;
  }

  size_t nRotas() const { return rotas.size(); }

  int         respCodigo = 0;
  std::string respTipo;
  String      respCorpo;
  size_t      respBytes = 0;

  static WebServer* atual;

 private:
  std::vector<Rota> rotas;
  std::map<std::string, std::string> args;
  std::string uriAtual;
  void (*naoEncontrado)() = nullptr;
};

// Atalhos usados pelo banco.
int         webPost(const std::string& alvo, const char* corpo = nullptr);
int         webGet (const std::string& alvo);
const char* webCorpo();
const char* webCabecalho(const char* nome);
