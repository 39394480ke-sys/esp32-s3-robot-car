#pragma once

#include "robot_types.h"

void robot_state_init(void);
void robot_state_record_motion(robot_motion_command_t motion,
                               uint8_t speed_percent,
                               bool watchdog_armed);
void robot_state_record_stop(robot_stop_reason_t reason);
void robot_state_set_estop(bool active);
robot_state_snapshot_t robot_state_get_snapshot(void);
