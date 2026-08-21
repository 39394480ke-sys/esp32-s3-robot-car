#pragma once

#include <stdbool.h>

#include "robot_types.h"

typedef enum {
    MOTOR_WHEEL_STOP = 0,
    MOTOR_WHEEL_FORWARD,
    MOTOR_WHEEL_REVERSE,
} motor_wheel_direction_t;

typedef struct {
    motor_wheel_direction_t left;
    motor_wheel_direction_t right;
} motor_direction_pair_t;

bool motor_policy_resolve(robot_motion_command_t command,
                          bool invert_left,
                          bool invert_right,
                          motor_direction_pair_t *directions);
