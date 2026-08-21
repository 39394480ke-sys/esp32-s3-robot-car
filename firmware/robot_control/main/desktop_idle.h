#pragma once

#include <stdbool.h>

#include "esp_err.h"

typedef enum {
    ROBOT_MODE_MANUAL = 0,
    ROBOT_MODE_DESKTOP_IDLE,
} robot_mode_t;

typedef enum {
    DESKTOP_IDLE_ACTION_NONE = 0,
    DESKTOP_IDLE_ACTION_LOOK_LEFT,
    DESKTOP_IDLE_ACTION_LOOK_RIGHT,
    DESKTOP_IDLE_ACTION_LOOK_UP,
} desktop_idle_action_t;

typedef struct {
    bool initialized;
    bool enabled;
    robot_mode_t mode;
    desktop_idle_action_t action;
} desktop_idle_snapshot_t;

esp_err_t desktop_idle_init(void);
esp_err_t desktop_idle_set_enabled(bool enabled);
void desktop_idle_record_user_activity(void);
void desktop_idle_cancel_for_estop(void);
desktop_idle_snapshot_t desktop_idle_get_snapshot(void);
const char *robot_mode_name(robot_mode_t mode);
const char *desktop_idle_action_name(desktop_idle_action_t action);
