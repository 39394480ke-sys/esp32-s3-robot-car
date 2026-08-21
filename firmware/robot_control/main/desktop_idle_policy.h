#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "servo_policy.h"

#define DESKTOP_IDLE_ENTRY_DELAY_MS 30000U
#define DESKTOP_IDLE_GESTURE_MIN_DELAY_MS 15000U
#define DESKTOP_IDLE_GESTURE_MAX_DELAY_MS 45000U
#define DESKTOP_IDLE_EXPRESSION_MIN_DELAY_MS 20000U
#define DESKTOP_IDLE_EXPRESSION_MAX_DELAY_MS 60000U
#define DESKTOP_IDLE_GESTURE_HOLD_MS 1500U
#define DESKTOP_IDLE_YAW_OFFSET_DEGREES 10
#define DESKTOP_IDLE_PITCH_OFFSET_DEGREES 8

bool desktop_idle_policy_should_enter(bool enabled,
                                      bool estop_active,
                                      bool motors_stopped,
                                      uint64_t now_ms,
                                      uint64_t last_activity_ms);
uint32_t desktop_idle_policy_random_delay(uint32_t random_value,
                                          uint32_t minimum_ms,
                                          uint32_t maximum_ms);
int16_t desktop_idle_policy_offset_angle(const servo_axis_limits_t *limits,
                                         int16_t offset_degrees);
