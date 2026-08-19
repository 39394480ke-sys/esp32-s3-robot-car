#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "camera_benchmark.h"
#include "camera_safety_check.h"
#include "hardware_info.h"

static const char *TAG = "d0_probe";

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-S3 camera benchmark D0 probe start");

    const bool psram_usable = hardware_info_run_probe();
    ESP_LOGI(TAG, "D0_RESULT=%s", psram_usable ? "PASS" : "FAIL");

    bool camera_safe = false;
    bool continuous_safe = false;
    camera_benchmark_result_t benchmark_result = CAMERA_BENCHMARK_FAIL;
    if (psram_usable) {
        camera_safe = camera_run_single_frame_safety_check();
        if (camera_safe) {
            continuous_safe = camera_run_continuous_safety_check();
        } else {
            ESP_LOGE(TAG, "continuous safety check skipped because single-frame check failed");
        }
        if (continuous_safe) {
            benchmark_result = camera_run_qqvga_short_diagnostic();
        } else {
            ESP_LOGE(TAG, "QQVGA diagnostic skipped because safety checks failed");
        }
    } else {
        ESP_LOGE(TAG, "D1 safety check skipped because D0 probe failed");
    }

    while (true) {
        ESP_LOGI(TAG,
                 "idle; camera deinitialized, Wi-Fi/motors/sensors disabled, "
                 "D1 safety=%s continuous=%s benchmark=%s",
                 camera_safe ? "PASS" : "FAIL",
                 continuous_safe ? "PASS" : "FAIL",
                 camera_benchmark_result_name(benchmark_result));
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
