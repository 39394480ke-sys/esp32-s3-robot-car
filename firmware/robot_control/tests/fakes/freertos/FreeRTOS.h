#pragma once

#include <stdint.h>

typedef int portMUX_TYPE;

#define portMUX_INITIALIZER_UNLOCKED 0
#define portMAX_DELAY UINT32_MAX
#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux) ((void)(mux))
