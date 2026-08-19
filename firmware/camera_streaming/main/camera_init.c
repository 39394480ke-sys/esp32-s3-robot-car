#include "camera_init.h"

#include "esp_camera.h"
#include "esp_log.h"
#include "sensor.h"

#include "camera_board_config.h"

static const char *TAG = "camera_init";

esp_err_t camera_init_baseline(void)
{
    const camera_config_t config = {
        .pin_pwdn = CAM_PIN_PWDN,
        .pin_reset = CAM_PIN_RESET,
        .pin_xclk = CAM_PIN_XCLK,
        .pin_sccb_sda = CAM_PIN_SIOD,
        .pin_sccb_scl = CAM_PIN_SIOC,
        .pin_d7 = CAM_PIN_D7,
        .pin_d6 = CAM_PIN_D6,
        .pin_d5 = CAM_PIN_D5,
        .pin_d4 = CAM_PIN_D4,
        .pin_d3 = CAM_PIN_D3,
        .pin_d2 = CAM_PIN_D2,
        .pin_d1 = CAM_PIN_D1,
        .pin_d0 = CAM_PIN_D0,
        .pin_vsync = CAM_PIN_VSYNC,
        .pin_href = CAM_PIN_HREF,
        .pin_pclk = CAM_PIN_PCLK,
        .xclk_freq_hz = 20000000,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_JPEG,
        .frame_size = FRAMESIZE_QVGA,
        .jpeg_quality = 20,
        .fb_count = 2,
        .fb_location = CAMERA_FB_IN_PSRAM,
        .grab_mode = CAMERA_GRAB_LATEST,
        .sccb_i2c_port = 1,
    };

    ESP_LOGI(TAG,
             "initializing OV3660: JPEG QVGA quality=20 XCLK=20MHz "
             "fb_count=2 PSRAM grab=LATEST PSRAM_DMA=OFF");
    esp_err_t result = esp_camera_init(&config);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "esp_camera_init failed: %s", esp_err_to_name(result));
        return result;
    }

    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor == NULL || sensor->id.PID != OV3660_PID) {
        ESP_LOGE(TAG, "expected OV3660 PID=0x%04x", OV3660_PID);
        esp_camera_deinit();
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (esp_camera_get_psram_mode()) {
        result = esp_camera_set_psram_mode(false);
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "unable to disable PSRAM DMA: %s", esp_err_to_name(result));
            esp_camera_deinit();
            return result;
        }
        sensor = esp_camera_sensor_get();
    }

    if (sensor == NULL || sensor->id.PID != OV3660_PID ||
        esp_camera_get_psram_mode()) {
        ESP_LOGE(TAG, "OV3660 or PSRAM DMA verification failed");
        esp_camera_deinit();
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG,
             "sensor PID=0x%04x; QVGA baseline verified; "
             "PCLK=10MHz; PSRAM DMA=OFF",
             sensor->id.PID);
    return ESP_OK;
}

esp_err_t camera_shutdown(void)
{
    const esp_err_t result = esp_camera_deinit();
    ESP_LOGI(TAG, "camera deinitialized: %s", esp_err_to_name(result));
    return result;
}
