#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int16_t minimum;
    int16_t center;
    int16_t maximum;
} servo_axis_limits_t;

int16_t servo_policy_clamp(int16_t angle, const servo_axis_limits_t *limits);
int16_t servo_policy_apply_trim(int16_t angle, int16_t trim);
uint32_t servo_policy_angle_to_duty(int16_t angle,
                                    bool inverted,
                                    uint32_t max_duty);
