// Sistema de arquivos em memoria: o suficiente para exercitar
// armazenamento.cpp no PC, incluindo cartao ausente e arquivo corrompido.
#pragma once
#include "Arduino.h"
#include <map>
#include <vector>
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdarg>

#define FILE_READ   "r"
#define FILE_WRITE  "w"
#define FILE_APPEND "a"

struct FsMock {
  std::map<std::string, std::string> arquivos;   // caminho -> conteudo
  std::vector<std::string> pastas;
  bool cartaoPresente = true;      // simula cartao fora do slot
  bool falharEscrita  = false;     // simula cartao cheio / travado
  uint64_t capacidade = 4ULL * 1024 * 1024 * 1024;
};
extern FsMock g_fs;

class File {
 public:
  File() {}
  File(const std::string& caminho, const char* modo)
      : cam(caminho), aberto(true), escrita(modo[0] != 'r') {
    if (escrita) {
      if (modo[0] == 'w') g_fs.arquivos[cam].clear();
      else                g_fs.arquivos[cam];      // append: cria se faltar
    } else {
      auto i = g_fs.arquivos.find(cam);
      if (i == g_fs.arquivos.end()) { aberto = false; return; }
      dados = i->second;
    }
  }

  explicit operator bool() const { return aberto; }
  bool operator!() const { return !aberto; }

  void close() { aberto = false; }
  const char* name() const { return cam.c_str(); }
  size_t size() const {
    auto i = g_fs.arquivos.find(cam);
    return i == g_fs.arquivos.end() ? 0 : i->second.size();
  }
  bool isDirectory() const { return ehPasta; }

  // ---- leitura ----
  int available() const { return (int)(dados.size() - pos); }
  int read() { return pos < dados.size() ? (unsigned char)dados[pos++] : -1; }
  size_t read(uint8_t* destino, size_t n) {
    const size_t d = dados.size() - pos;
    const size_t k = n < d ? n : d;
    memcpy(destino, dados.data() + pos, k);
    pos += k;
    return k;
  }

  // ---- escrita ----
  size_t write(const uint8_t* origem, size_t n) {
    if (g_fs.falharEscrita) return 0;
    g_fs.arquivos[cam].append((const char*)origem, n);
    return n;
  }
  size_t print(const char* s) { return write((const uint8_t*)s, strlen(s)); }
  size_t println(const char* s) { const size_t n = print(s); return n + print("\n"); }
  size_t printf(const char* fmt, ...) {
    char buf[512];
    va_list a; va_start(a, fmt);
    const int n = vsnprintf(buf, sizeof(buf), fmt, a);
    va_end(a);
    return n > 0 ? write((const uint8_t*)buf, (size_t)n) : 0;
  }

  // ---- iteracao de pasta ----
  File openNextFile();

  std::string cam;
  bool aberto = false;
  bool escrita = false;
  bool ehPasta = false;
  std::string dados;
  size_t pos = 0;
  std::vector<std::string> filhos;
  size_t idxFilho = 0;
};

inline File File::openNextFile() {
  while (idxFilho < filhos.size()) {
    const std::string& f = filhos[idxFilho++];
    if (g_fs.arquivos.count(f)) return File(f, FILE_READ);
  }
  return File();
}
