#include "camera_safety_check.h"

#include <stdint.h>

#include "esp_camera.h"
#include "esp_log.h"

#include "camera_init.h"

static const char *TAG = "camera_safety";

#define CONTINUOUS_FRAME_COUNT 60U

bool camera_frame_is_valid(const camera_fb_t *frame)
{
    return frame != NULL &&
           frame->format == PIXFORMAT_JPEG &&
           frame->width == 320U &&
           frame->height == 240U &&
           frame->len >= 4U &&
           frame->buf[0] == 0xffU &&
           frame->buf[1] == 0xd8U &&
           frame->buf[frame->len - 2U] == 0xffU &&
           frame->buf[frame->len - 1U] == 0xd9U;
}

bool camera_run_single_frame_safety_check(void)
{
    if (camera_init_baseline() != ESP_OK) {
        return false;
    }

    camera_fb_t *frame = esp_camera_fb_get();
    const bool valid = camera_frame_is_valid(frame);
    if (frame != NULL) {
        ESP_LOGI(TAG,
                 "single frame: width=%u height=%u format=%d size=%u valid=%s",
                 frame->width,
                 frame->height,
                 frame->format,
                 (unsigned)frame->len,
                 valid ? "YES" : "NO");
        esp_camera_fb_return(frame);
    }

    const esp_err_t shutdown_result = camera_shutdown();
    const bool passed = valid && shutdown_result == ESP_OK;
    ESP_LOGI(TAG, "D2_CAMERA_SINGLE_FRAME_RESULT=%s", passed ? "PASS" : "FAIL");
    return passed;
}

bool camera_run_continuous_safety_check(void)
{
    if (camera_init_baseline() != ESP_OK) {
        return false;
    }

    unsigned successful = 0U;
    unsigned failed = 0U;
    for (unsigned index = 0U; index < CONTINUOUS_FRAME_COUNT; ++index) {
        camera_fb_t *frame = esp_camera_fb_get();
        if (camera_frame_is_valid(frame)) {
            ++successful;
        } else {
            ++failed;
        }
        if (frame != NULL) {
            esp_camera_fb_return(frame);
        }
    }

    const esp_err_t shutdown_result = camera_shutdown();
    const bool passed = successful == CONTINUOUS_FRAME_COUNT &&
                        failed == 0U &&
                        shutdown_result == ESP_OK;
    ESP_LOGI(TAG,
             "continuous frames: requested=%u successful=%u failed=%u",
             CONTINUOUS_FRAME_COUNT,
             successful,
             failed);
    ESP_LOGI(TAG, "D2_CAMERA_CONTINUOUS_RESULT=%s", passed ? "PASS" : "FAIL");
    return passed;
}
