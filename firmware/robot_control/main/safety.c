#include "safety.h"

#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_timer.h"

typedef struct {
    safety_poll_callback_t callback;
    void *context;
} safety_task_context_t;

static safety_task_context_t s_task_context;
static TaskHandle_t s_task_handle;

bool safety_watchdog_expired(bool armed,
                             uint64_t last_command_ms,
                             uint64_t now_ms,
                             uint32_t timeout_ms)
{
    return armed && now_ms >= last_command_ms &&
           now_ms - last_command_ms >= timeout_ms;
}

static void safety_task(void *argument)
{
    safety_task_context_t *task_context = argument;
    while (true) {
        const uint64_t now_ms = (uint64_t)(esp_timer_get_time() / 1000);
        task_context->callback(now_ms, task_context->context);
        vTaskDelay(pdMS_TO_TICKS(ROBOT_CONTROL_WATCHDOG_POLL_MS));
    }
}

esp_err_t safety_start_polling(safety_poll_callback_t callback, void *context)
{
    if (callback == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_task_handle != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    s_task_context.callback = callback;
    s_task_context.context = context;
    if (xTaskCreate(safety_task,
                    "robot_safety",
                    3072U,
                    &s_task_context,
                    10U,
                    &s_task_handle) != pdPASS) {
        s_task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
