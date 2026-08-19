#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_camera.h"
#include "esp_err.h"

esp_err_t camera_init_baseline(void);
esp_err_t camera_init_with_quality(uint8_t jpeg_quality);
esp_err_t camera_init_with_settings(uint8_t jpeg_quality, uint8_t fb_count);
esp_err_t camera_init_with_dma_settings(uint8_t jpeg_quality,
                                        uint8_t fb_count,
                                        bool psram_dma);
esp_err_t camera_init_with_frame_settings(uint8_t jpeg_quality,
                                          uint8_t fb_count,
                                          bool psram_dma,
                                          framesize_t frame_size);
esp_err_t camera_apply_ov3660_pclk_div(uint8_t pclk_div);
esp_err_t camera_shutdown(void);
