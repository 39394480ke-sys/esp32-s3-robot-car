#pragma once

#include <stdbool.h>

#include "freertos/FreeRTOS.h"

typedef void *SemaphoreHandle_t;

static inline SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    return (SemaphoreHandle_t)1;
}

static inline int xSemaphoreTake(SemaphoreHandle_t semaphore, uint32_t timeout)
{
    (void)semaphore;
    (void)timeout;
    return true;
}

static inline int xSemaphoreGive(SemaphoreHandle_t semaphore)
{
    (void)semaphore;
    return true;
}
