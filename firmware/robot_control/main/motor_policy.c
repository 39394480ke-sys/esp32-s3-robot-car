#include "motor_policy.h"

#include <stddef.h>

static motor_wheel_direction_t invert_direction(motor_wheel_direction_t direction)
{
    if (direction == MOTOR_WHEEL_FORWARD) {
        return MOTOR_WHEEL_REVERSE;
    }
    if (direction == MOTOR_WHEEL_REVERSE) {
        return MOTOR_WHEEL_FORWARD;
    }
    return MOTOR_WHEEL_STOP;
}

bool motor_policy_resolve(robot_motion_command_t command,
                          bool invert_left,
                          bool invert_right,
                          motor_direction_pair_t *directions)
{
    if (directions == NULL) {
        return false;
    }

    motor_direction_pair_t resolved = {
        .left = MOTOR_WHEEL_STOP,
        .right = MOTOR_WHEEL_STOP,
    };

    switch (command) {
    case ROBOT_MOTION_STOP:
        break;
    case ROBOT_MOTION_FORWARD:
        resolved.left = MOTOR_WHEEL_FORWARD;
        resolved.right = MOTOR_WHEEL_FORWARD;
        break;
    case ROBOT_MOTION_BACKWARD:
        resolved.left = MOTOR_WHEEL_REVERSE;
        resolved.right = MOTOR_WHEEL_REVERSE;
        break;
    case ROBOT_MOTION_TURN_LEFT:
        resolved.left = MOTOR_WHEEL_REVERSE;
        resolved.right = MOTOR_WHEEL_FORWARD;
        break;
    case ROBOT_MOTION_TURN_RIGHT:
        resolved.left = MOTOR_WHEEL_FORWARD;
        resolved.right = MOTOR_WHEEL_REVERSE;
        break;
    default:
        return false;
    }

    if (invert_left) {
        resolved.left = invert_direction(resolved.left);
    }
    if (invert_right) {
        resolved.right = invert_direction(resolved.right);
    }
    *directions = resolved;
    return true;
}
