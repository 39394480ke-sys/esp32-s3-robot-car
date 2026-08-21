#include "oled_expression.h"

#include <stddef.h>
#include <string.h>

static const char *const EXPRESSION_NAMES[OLED_EXPRESSION_COUNT] = {
    [OLED_EXPRESSION_IDLE] = "idle",
    [OLED_EXPRESSION_HAPPY] = "happy",
    [OLED_EXPRESSION_CURIOUS] = "curious",
    [OLED_EXPRESSION_CONFUSED] = "confused",
    [OLED_EXPRESSION_SLEEPY] = "sleepy",
    [OLED_EXPRESSION_WATCHING] = "watching",
    [OLED_EXPRESSION_WARNING] = "warning",
    [OLED_EXPRESSION_EXCITED] = "excited",
};

bool oled_expression_from_name(const char *name, oled_expression_id_t *expression)
{
    if (name == NULL || expression == NULL) {
        return false;
    }
    for (int index = 0; index < OLED_EXPRESSION_COUNT; ++index) {
        if (strcmp(name, EXPRESSION_NAMES[index]) == 0) {
            *expression = (oled_expression_id_t)index;
            return true;
        }
    }
    return false;
}
const char *oled_expression_name(oled_expression_id_t expression)
{
    if (expression < 0 || expression >= OLED_EXPRESSION_COUNT) {
        return "unknown";
    }
    return EXPRESSION_NAMES[expression];
}
