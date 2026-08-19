#pragma once

#include <stdbool.h>

#include "esp_camera.h"

bool camera_frame_is_valid(const camera_fb_t *frame);
bool camera_run_single_frame_safety_check(void);
bool camera_run_continuous_safety_check(void);
