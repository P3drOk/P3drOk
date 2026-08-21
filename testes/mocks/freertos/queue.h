#pragma once
#include "FreeRTOS.h"
#include <deque>
#include <vector>
#include <cstring>

struct FilaMock {
  size_t capacidade = 0;
  size_t tamItem = 0;
  std::deque<std::vector<unsigned char>> itens;
};
typedef FilaMock* QueueHandle_t;

extern int g_comandosDescartados;   // quantos xQueueSend falharam

inline QueueHandle_t xQueueCreate(size_t n, size_t tam) {
  FilaMock* f = new FilaMock();
  f->capacidade = n; f->tamItem = tam; return f;
}
inline BaseType_t xQueueSend(QueueHandle_t f, const void* item, uint32_t) {
  if (!f || f->itens.size() >= f->capacidade) { g_comandosDescartados++; return pdFALSE; }
  std::vector<unsigned char> v(f->tamItem);
  memcpy(v.data(), item, f->tamItem);
  f->itens.push_back(std::move(v));
  return pdTRUE;
}
inline BaseType_t xQueueReceive(QueueHandle_t f, void* destino, uint32_t) {
  if (!f || f->itens.empty()) return pdFALSE;
  memcpy(destino, f->itens.front().data(), f->tamItem);
  f->itens.pop_front();
  return pdTRUE;
}
