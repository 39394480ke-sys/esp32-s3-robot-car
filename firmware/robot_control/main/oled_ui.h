#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "oled_expression.h"

typedef struct {
    bool initialized;
    uint8_t i2c_address;
    oled_expression_id_t expression;
} oled_ui_snapshot_t;

esp_err_t oled_ui_init(void);
esp_err_t oled_set_expression(const char *expression_name);
oled_ui_snapshot_t oled_ui_get_snapshot(void);
