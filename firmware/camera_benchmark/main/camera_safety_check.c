#include "camera_safety_check.h"

#include "esp_camera.h"
#include "esp_log.h"

#include "camera_init.h"

static const char *TAG = "camera_safety";

#define CONTINUOUS_FRAME_COUNT 60U

static bool frame_is_valid(const camera_fb_t *frame)
{
    return frame != NULL &&
           frame->format == PIXFORMAT_JPEG &&
           frame->width == 320U &&
           frame->height == 240U &&
           frame->len >= 2U &&
           frame->buf[0] == 0xffU &&
           frame->buf[1] == 0xd8U;
}

bool camera_run_single_frame_safety_check(void)
{
    const esp_err_t init_result = camera_init_baseline();
    if (init_result != ESP_OK) {
        ESP_LOGE(TAG, "D1 safety check stopped at camera initialization");
        return false;
    }

    bool frame_valid = false;
    camera_fb_t *frame = esp_camera_fb_get();
    if (frame == NULL) {
        ESP_LOGE(TAG, "single-frame capture failed");
    } else {
        const bool jpeg_marker_valid =
            frame->len >= 2U && frame->buf[0] == 0xffU && frame->buf[1] == 0xd8U;
        frame_valid = frame_is_valid(frame);

        ESP_LOGI(TAG,
                 "single frame: width=%u height=%u format=%d size=%u bytes "
                 "jpeg_soi=%s",
                 frame->width,
                 frame->height,
                 frame->format,
                 (unsigned)frame->len,
                 jpeg_marker_valid ? "YES" : "NO");
        esp_camera_fb_return(frame);
        ESP_LOGI(TAG, "framebuffer returned");
    }

    const bool shutdown_ok = camera_shutdown() == ESP_OK;
    const bool passed = frame_valid && shutdown_ok;
    ESP_LOGI(TAG, "D1_SAFETY_RESULT=%s", passed ? "PASS" : "FAIL");
    return passed;
}

bool camera_run_continuous_safety_check(void)
{
    const esp_err_t init_result = camera_init_baseline();
    if (init_result != ESP_OK) {
        ESP_LOGE(TAG, "continuous safety check stopped at camera initialization");
        return false;
    }

    unsigned successful_frames = 0U;
    unsigned failed_frames = 0U;
    size_t minimum_size = SIZE_MAX;
    size_t maximum_size = 0U;

    for (unsigned index = 0U; index < CONTINUOUS_FRAME_COUNT; ++index) {
        camera_fb_t *frame = esp_camera_fb_get();
        if (!frame_is_valid(frame)) {
            ++failed_frames;
        } else {
            ++successful_frames;
            if (frame->len < minimum_size) {
                minimum_size = frame->len;
            }
            if (frame->len > maximum_size) {
                maximum_size = frame->len;
            }
        }

        if (frame != NULL) {
            esp_camera_fb_return(frame);
        }
    }

    const bool shutdown_ok = camera_shutdown() == ESP_OK;
    const bool passed = successful_frames == CONTINUOUS_FRAME_COUNT &&
                        failed_frames == 0U &&
                        shutdown_ok;
    ESP_LOGI(TAG,
             "continuous frames: requested=%u successful=%u failed=%u "
             "jpeg_size_min=%u jpeg_size_max=%u",
             CONTINUOUS_FRAME_COUNT,
             successful_frames,
             failed_frames,
             (unsigned)(successful_frames > 0U ? minimum_size : 0U),
             (unsigned)maximum_size);
    ESP_LOGI(TAG, "D1_CONTINUOUS_RESULT=%s", passed ? "PASS" : "FAIL");
    return passed;
}
