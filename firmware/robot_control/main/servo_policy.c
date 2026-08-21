#include "servo_policy.h"

#include <stddef.h>

#define SERVO_MIN_ANGLE 0
#define SERVO_MAX_ANGLE 180
#define SERVO_MIN_PULSE_US 500U
#define SERVO_MAX_PULSE_US 2500U
#define SERVO_PERIOD_US 20000U

int16_t servo_policy_clamp(int16_t angle, const servo_axis_limits_t *limits)
{
    if (limits == NULL) {
        return angle;
    }
    if (angle < limits->minimum) {
        return limits->minimum;
    }
    if (angle > limits->maximum) {
        return limits->maximum;
    }
    return angle;
}

int16_t servo_policy_apply_trim(int16_t angle, int16_t trim)
{
    const int32_t calibrated = (int32_t)angle + trim;
    if (calibrated < SERVO_MIN_ANGLE) {
        return SERVO_MIN_ANGLE;
    }
    if (calibrated > SERVO_MAX_ANGLE) {
        return SERVO_MAX_ANGLE;
    }
    return (int16_t)calibrated;
}

uint32_t servo_policy_angle_to_duty(int16_t angle,
                                    bool inverted,
                                    uint32_t max_duty)
{
    if (angle < SERVO_MIN_ANGLE) {
        angle = SERVO_MIN_ANGLE;
    } else if (angle > SERVO_MAX_ANGLE) {
        angle = SERVO_MAX_ANGLE;
    }
    if (inverted) {
        angle = SERVO_MAX_ANGLE - angle;
    }

    const uint32_t pulse_us = SERVO_MIN_PULSE_US +
        ((uint32_t)angle * (SERVO_MAX_PULSE_US - SERVO_MIN_PULSE_US) + 90U) /
            SERVO_MAX_ANGLE;
    return (pulse_us * max_duty + (SERVO_PERIOD_US / 2U)) /
        SERVO_PERIOD_US;
}
