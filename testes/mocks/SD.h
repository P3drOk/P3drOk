#pragma once
#include "FS.h"
#include "SPI.h"

#define CARD_NONE 0
#define CARD_SD   1

class SDMock {
 public:
  bool begin(uint8_t, SPIClass&, uint32_t) {
    montado = g_fs.cartaoPresente;
    return montado;
  }
  void end() { montado = false; }
  int  cardType() const { return montado ? CARD_SD : CARD_NONE; }
  uint64_t totalBytes() const { return g_fs.capacidade; }
  uint64_t usedBytes() const {
    uint64_t t = 0;
    for (auto& a : g_fs.arquivos) t += a.second.size();
    return t;
  }
  bool exists(const char* c) {
    if (g_fs.arquivos.count(c)) return true;
    for (auto& p : g_fs.pastas) if (p == c) return true;
    return false;
  }
  bool mkdir(const char* c) { g_fs.pastas.push_back(c); return true; }
  bool remove(const char* c) { return g_fs.arquivos.erase(c) > 0; }

  File open(const char* caminho, const char* modo = FILE_READ) {
    if (!montado) return File();
    // Pasta: devolve um File iteravel com os filhos diretos.
    for (auto& p : g_fs.pastas) {
      if (p == caminho) {
        File d;
        d.aberto = true; d.ehPasta = true; d.cam = caminho;
        const std::string prefixo = std::string(caminho) + "/";
        for (auto& a : g_fs.arquivos) {
          if (a.first.compare(0, prefixo.size(), prefixo) == 0 &&
              a.first.find('/', prefixo.size()) == std::string::npos) {
            d.filhos.push_back(a.first);
          }
        }
        return d;
      }
    }
    if (modo[0] == 'r' && !g_fs.arquivos.count(caminho)) return File();
    return File(caminho, modo);
  }

  bool montado = false;
};
extern SDMock SD;
