#pragma once

#include <stdbool.h>

typedef enum {
    OLED_EXPRESSION_IDLE = 0,
    OLED_EXPRESSION_HAPPY,
    OLED_EXPRESSION_CURIOUS,
    OLED_EXPRESSION_CONFUSED,
    OLED_EXPRESSION_SLEEPY,
    OLED_EXPRESSION_WATCHING,
    OLED_EXPRESSION_WARNING,
    OLED_EXPRESSION_EXCITED,
    OLED_EXPRESSION_COUNT,
} oled_expression_id_t;

bool oled_expression_from_name(const char *name, oled_expression_id_t *expression);
const char *oled_expression_name(oled_expression_id_t expression);
