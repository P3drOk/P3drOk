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
inline void vTaskDelay(uint32_t) {}
typedef int BaseType_t;
inline BaseType_t xTaskCreatePinnedToCore(void (*)(void*), const char*, uint32_t,
                                          void*, uint32_t, void*, int) { return pdTRUE; }
