#include "camera_init.h"

#include "esp_camera.h"
#include "esp_log.h"
#include "sensor.h"

#include "camera_board_config.h"

static const char *TAG = "camera_init";

#define OV3660_PCLK_RATIO_REG 0x3824

static const char *frame_size_name(framesize_t frame_size)
{
    switch (frame_size) {
    case FRAMESIZE_QQVGA:
        return "QQVGA";
    case FRAMESIZE_QVGA:
        return "QVGA";
    default:
        return "UNKNOWN";
    }
}

esp_err_t camera_init_with_frame_settings(uint8_t jpeg_quality,
                                          uint8_t fb_count,
                                          bool psram_dma,
                                          framesize_t frame_size)
{
    camera_config_t config = {
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
    config.frame_size = frame_size;
    config.jpeg_quality = jpeg_quality;
    config.fb_count = fb_count;

    ESP_LOGI(TAG,
             "initializing OV3660: JPEG %s quality=%u XCLK=20MHz "
             "fb_count=%u PSRAM grab=LATEST PSRAM_DMA=%s",
             frame_size_name(frame_size),
             jpeg_quality,
             fb_count,
             psram_dma ? "ON" : "OFF");

    const esp_err_t result = esp_camera_init(&config);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "esp_camera_init failed: %s (0x%x)",
                 esp_err_to_name(result), result);
        return result;
    }

    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor == NULL) {
        ESP_LOGE(TAG, "camera initialized without a sensor handle");
        esp_camera_deinit();
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG,
             "sensor: PID=0x%04x VER=0x%02x MIDL=0x%02x MIDH=0x%02x",
             sensor->id.PID,
             sensor->id.VER,
             sensor->id.MIDL,
             sensor->id.MIDH);

    if (sensor->id.PID != OV3660_PID) {
        ESP_LOGE(TAG, "expected OV3660 PID=0x%04x, detected PID=0x%04x",
                 OV3660_PID, sensor->id.PID);
        esp_camera_deinit();
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (esp_camera_get_psram_mode() != psram_dma) {
        const esp_err_t dma_result = esp_camera_set_psram_mode(psram_dma);
        if (dma_result != ESP_OK) {
            ESP_LOGE(TAG,
                     "unable to switch PSRAM DMA mode %s: %s",
                     psram_dma ? "ON" : "OFF",
                     esp_err_to_name(dma_result));
            esp_camera_deinit();
            return dma_result;
        }

        sensor = esp_camera_sensor_get();
        if (sensor == NULL || sensor->id.PID != OV3660_PID) {
            ESP_LOGE(TAG, "OV3660 identity lost after PSRAM DMA reconfiguration");
            esp_camera_deinit();
            return ESP_ERR_INVALID_RESPONSE;
        }
    }

    if (esp_camera_get_psram_mode() != psram_dma) {
        ESP_LOGE(TAG, "PSRAM DMA state does not match requested state");
        esp_camera_deinit();
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG,
             "OV3660 identity check: PASS; PSRAM DMA verification: %s",
             psram_dma ? "ON" : "OFF");
    return ESP_OK;
}

esp_err_t camera_apply_ov3660_pclk_div(uint8_t pclk_div)
{
    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor == NULL || sensor->id.PID != OV3660_PID) {
        ESP_LOGE(TAG, "cannot apply PCLK divisor without an active OV3660");
        return ESP_ERR_INVALID_STATE;
    }
    if (sensor->set_pll == NULL || sensor->get_reg == NULL) {
        ESP_LOGE(TAG, "OV3660 timing controls are unavailable");
        return ESP_ERR_NOT_SUPPORTED;
    }

    const int result = sensor->set_pll(sensor,
                                       0,
                                       30,
                                       1,
                                       0,
                                       3,
                                       0,
                                       1,
                                       pclk_div);
    if (result != 0) {
        ESP_LOGE(TAG, "OV3660 PCLK divisor %u update failed", pclk_div);
        return ESP_FAIL;
    }

    const int verified = sensor->get_reg(sensor, OV3660_PCLK_RATIO_REG, 0x1f);
    if (verified < 0 || (verified & 0x1f) != pclk_div) {
        ESP_LOGE(TAG,
                 "OV3660 PCLK divisor read-back failed: requested=%u read=%d",
                 pclk_div,
                 verified);
        return ESP_ERR_INVALID_RESPONSE;
    }

    ESP_LOGI(TAG,
             "OV3660_PCLK_DIV_VERIFIED=%u register=0x%04x",
             pclk_div,
             OV3660_PCLK_RATIO_REG);
    return ESP_OK;
}

esp_err_t camera_init_with_dma_settings(uint8_t jpeg_quality,
                                        uint8_t fb_count,
                                        bool psram_dma)
{
    return camera_init_with_frame_settings(jpeg_quality,
                                           fb_count,
                                           psram_dma,
                                           FRAMESIZE_QVGA);
}

esp_err_t camera_init_with_settings(uint8_t jpeg_quality, uint8_t fb_count)
{
    return camera_init_with_dma_settings(jpeg_quality, fb_count, false);
}

esp_err_t camera_init_with_quality(uint8_t jpeg_quality)
{
    return camera_init_with_settings(jpeg_quality, 2U);
}

esp_err_t camera_init_baseline(void)
{
    return camera_init_with_quality(20U);
}

esp_err_t camera_shutdown(void)
{
    const esp_err_t result = esp_camera_deinit();
    if (result == ESP_OK) {
        ESP_LOGI(TAG, "camera deinitialized");
    } else {
        ESP_LOGE(TAG, "camera deinit failed: %s", esp_err_to_name(result));
    }
    return result;
}
