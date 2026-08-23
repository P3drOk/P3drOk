#pragma once
#include <cstdint>
#include <cstddef>
#define pdTRUE  1
#define pdFALSE 0
#define portMUX_TYPE int
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(m) ((void)(m))
#define portEXIT_CRITICAL(m)  ((void)(m))
#define pdMS_TO_TICKS(x) (x)
// vTaskDelay dorme de verdade no ESP32: o relogio anda enquanto a tarefa
// esta parada. Um mock vazio faria um laco que espera por tempo girar para
// sempre no banco -- e esconderia justamente o defeito que ele deveria
// mostrar. A assinatura do mock e a assinatura do core, e o efeito tambem.
extern uint32_t g_millis;
inline void vTaskDelay(uint32_t ticks) { g_millis += ticks; }
typedef int BaseType_t;
inline BaseType_t xTaskCreatePinnedToCore(void (*)(void*), const char*, uint32_t,
                                          void*, uint32_t, void*, int) { return pdTRUE; }
