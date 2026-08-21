#pragma once

#include <stdint.h>

#include "freertos/FreeRTOS.h"

typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);

#define pdPASS 1
#define pdMS_TO_TICKS(milliseconds) (milliseconds)

static inline int xTaskCreate(TaskFunction_t task,
                              const char *name,
                              uint32_t stack_depth,
                              void *argument,
                              unsigned priority,
                              TaskHandle_t *handle)
{
    (void)task;
    (void)name;
    (void)stack_depth;
    (void)argument;
    (void)priority;
    *handle = (TaskHandle_t)1;
    return pdPASS;
}

static inline void vTaskDelay(uint32_t ticks)
{
    (void)ticks;
}
