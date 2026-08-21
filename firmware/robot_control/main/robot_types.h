#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ROBOT_MOTION_STOP = 0,
    ROBOT_MOTION_FORWARD,
    ROBOT_MOTION_BACKWARD,
    ROBOT_MOTION_TURN_LEFT,
    ROBOT_MOTION_TURN_RIGHT,
} robot_motion_command_t;

typedef enum {
    ROBOT_STOP_REASON_BOOT = 0,
    ROBOT_STOP_REASON_EXPLICIT,
    ROBOT_STOP_REASON_ZERO_SPEED,
    ROBOT_STOP_REASON_INVALID_COMMAND,
    ROBOT_STOP_REASON_WATCHDOG_TIMEOUT,
    ROBOT_STOP_REASON_WIFI_DISCONNECTED,
    ROBOT_STOP_REASON_DRIVER_ERROR,
    ROBOT_STOP_REASON_INIT_FAILURE,
    ROBOT_STOP_REASON_ESTOP,
} robot_stop_reason_t;

typedef struct {
    robot_motion_command_t motion;
    uint8_t speed_percent;
    bool watchdog_armed;
    bool estop;
    robot_stop_reason_t last_stop_reason;
} robot_state_snapshot_t;
