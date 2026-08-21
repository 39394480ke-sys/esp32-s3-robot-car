#pragma once

#include <stdint.h>

#include "esp_err.h"

#include "robot_types.h"

#define ROBOT_DEFAULT_SPEED_PERCENT 40U

esp_err_t robot_control_init(void);
esp_err_t robot_drive(robot_motion_command_t command, uint8_t speed_percent);
esp_err_t robot_move_forward(uint8_t speed_percent);
esp_err_t robot_move_backward(uint8_t speed_percent);
esp_err_t robot_turn_left(uint8_t speed_percent);
esp_err_t robot_turn_right(uint8_t speed_percent);
void robot_stop(void);
void robot_notify_wifi_disconnected(void);
void robot_estop_activate(void);
void robot_estop_clear(void);
bool robot_estop_is_active(void);
