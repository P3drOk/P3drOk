#include "Arduino.h"
#include "Preferences.h"
#include "WiFi.h"
#include "SD.h"
#include "freertos/queue.h"

uint32_t g_millis = 0;
int g_pinModo[64]    = {0};
int g_pinSaida[64]   = {0};
int g_pinEntrada[64];
// Entradas em repouso: nivel alto. Um inicializador {1} so preencheria o
// primeiro elemento -- o resto ficaria em 0 e o firmware leria emergencia
// acionada em todos os testes.
static const bool _entradasEmRepouso = []{
  for (int i = 0; i < 64; i++) g_pinEntrada[i] = 1;
  return true;
}();
int g_escritasRele   = 0;
int g_subidas[64]    = {0};
bool g_serialSilencioso = true;
uint32_t g_serialBytes = 0;
uint32_t g_msgCount = 0;
int g_comandosDescartados = 0;

SerialMock Serial;
EspMock    ESP;
WiFiMock   WiFi;
NvsMock    g_nvs;
FsMock     g_fs;
SDMock     SD;

void pinMode(uint8_t p, int m) {
  if (p >= 64) return;
  g_pinModo[p] = m;
  if (m == INPUT_PULLUP) g_pinEntrada[p] = 1;
}
void digitalWrite(uint8_t p, int v) {
  if (p >= 64) return;
  if (v && !g_pinSaida[p]) g_subidas[p]++;
  g_pinSaida[p] = v;
}
int digitalRead(uint8_t p) { return p < 64 ? g_pinEntrada[p] : 1; }

// O servidor web nao entra no banco de testes (roda no core 0 e depende
// de rede). Ficam os stubs para o .ino compilar.
void servidorIniciar() {}
void servidorAtender() {}
