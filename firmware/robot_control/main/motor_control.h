#pragma once

#include <stdint.h>

#include "esp_err.h"

#include "robot_types.h"

esp_err_t motor_control_init(void);
esp_err_t motor_control_apply(robot_motion_command_t command,
                              uint8_t speed_percent);
esp_err_t motor_control_stop(void);
