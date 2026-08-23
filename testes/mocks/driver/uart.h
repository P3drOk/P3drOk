// Mock do driver de UART do IDF -- so o que o modulo do encoder usa para
// pedir o modo RS485 meio-duplex por HARDWARE.
//
// A assinatura e a do IDF, e o mock guarda o que foi pedido para o banco
// poder conferir. Um mock que aceitasse qualquer coisa deixaria o banco
// passar limpo e jogaria o erro na IDE do operador.
#pragma once
#include <cstdint>

#define UART_NUM_0 0
#define UART_NUM_1 1
#define UART_NUM_2 2
#define UART_PIN_NO_CHANGE (-1)

typedef int uart_port_t;
typedef int esp_err_t;
#define ESP_OK 0

typedef enum {
  UART_MODE_UART = 0,
  UART_MODE_RS485_HALF_DUPLEX = 1,
} uart_mode_t;

struct UartIdfMock {
  int modo    = UART_MODE_UART;
  int pinoRts = UART_PIN_NO_CHANGE;
  int pinoTx  = UART_PIN_NO_CHANGE;
  int pinoRx  = UART_PIN_NO_CHANGE;
  uint32_t chamadasModo = 0;
};
extern UartIdfMock g_uartIdf;

inline esp_err_t uart_set_pin(uart_port_t, int tx, int rx, int rts, int) {
  g_uartIdf.pinoTx = tx; g_uartIdf.pinoRx = rx; g_uartIdf.pinoRts = rts;
  return ESP_OK;
}
inline esp_err_t uart_set_mode(uart_port_t, uart_mode_t modo) {
  g_uartIdf.modo = (int)modo;
  g_uartIdf.chamadasModo++;
  return ESP_OK;
}
