#include "Arduino.h"
#include "Preferences.h"
#include "WiFi.h"
#include "freertos/queue.h"

uint32_t g_millis = 0;
int g_pinModo[64]    = {0};
int g_pinSaida[64]   = {0};
int g_pinEntrada[64] = {1};   // entradas em repouso: nivel alto (pull-up)
int g_escritasRele   = 0;
bool g_serialSilencioso = true;
uint32_t g_serialBytes = 0;
uint32_t g_msgCount = 0;
int g_comandosDescartados = 0;

SerialMock Serial;
WiFiMock   WiFi;
NvsMock    g_nvs;

void pinMode(uint8_t p, int m) { if (p < 64) g_pinModo[p] = m; }
void digitalWrite(uint8_t p, int v) {
  if (p >= 64) return;
  g_pinSaida[p] = v;
}
int digitalRead(uint8_t p) { return p < 64 ? g_pinEntrada[p] : 1; }

// O servidor web nao entra no banco de testes (roda no core 0 e depende
// de rede). Ficam os stubs para o .ino compilar.
void servidorIniciar() {}
void servidorAtender() {}
