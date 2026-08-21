#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define ROBOT_CONTROL_WATCHDOG_TIMEOUT_MS 800U
#define ROBOT_CONTROL_WATCHDOG_POLL_MS 20U

typedef void (*safety_poll_callback_t)(uint64_t now_ms, void *context);

bool safety_watchdog_expired(bool armed,
                             uint64_t last_command_ms,
                             uint64_t now_ms,
                             uint32_t timeout_ms);
esp_err_t safety_start_polling(safety_poll_callback_t callback, void *context);
