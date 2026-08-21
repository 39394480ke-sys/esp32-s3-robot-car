#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "servo_policy.h"

typedef struct {
    bool initialized;
    int16_t yaw;
    int16_t pitch;
    servo_axis_limits_t yaw_limits;
    servo_axis_limits_t pitch_limits;
} servo_control_snapshot_t;

esp_err_t servo_control_init(void);
esp_err_t servo_set_yaw(int16_t angle);
esp_err_t servo_set_pitch(int16_t angle);
servo_control_snapshot_t servo_control_get_snapshot(void);

esp_err_t look_center(void);
esp_err_t look_left(void);
esp_err_t look_right(void);
esp_err_t look_up(void);
esp_err_t look_down(void);
esp_err_t nod(void);
esp_err_t shake_head(void);
