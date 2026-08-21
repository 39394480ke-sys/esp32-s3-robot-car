#include "esp_log.h"

#include "desktop_idle.h"
#include "oled_ui.h"
#include "robot_control.h"
#include "servo_control.h"
#include "tts_control.h"
#include "web_server.h"
#include "wifi_manager.h"

static const char *TAG = "robot_main";

void app_main(void)
{
    ESP_LOGI(TAG, "Stage B desktop idle initialization");
    const esp_err_t result = robot_control_init();
    if (result != ESP_OK) {
        ESP_LOGE(TAG,
                 "robot control initialization failed: %s; motors remain stopped",
                 esp_err_to_name(result));
        return;
    }

    const esp_err_t servo_result = servo_control_init();
    if (servo_result != ESP_OK) {
        robot_stop();
        ESP_LOGE(TAG,
                 "gimbal initialization failed: %s; motors remain stopped",
                 esp_err_to_name(servo_result));
        return;
    }

    const esp_err_t oled_result = oled_ui_init();
    if (oled_result != ESP_OK) {
        robot_stop();
        ESP_LOGW(TAG,
                 "OLED initialization failed: %s; continuing with OLED unavailable",
                 esp_err_to_name(oled_result));
    }

    const esp_err_t tts_result = tts_control_init();
    if (tts_result != ESP_OK) {
        robot_stop();
        ESP_LOGW(TAG,
                 "TTS initialization failed: %s; continuing with TTS unavailable",
                 esp_err_to_name(tts_result));
    }

    const esp_err_t wifi_result = wifi_manager_init();
    if (wifi_result != ESP_OK) {
        robot_stop();
        ESP_LOGE(TAG,
                 "Wi-Fi initialization failed: %s; motors remain stopped",
                 esp_err_to_name(wifi_result));
        return;
    }

    const esp_err_t idle_result = desktop_idle_init();
    if (idle_result != ESP_OK) {
        robot_stop();
        ESP_LOGE(TAG,
                 "desktop idle initialization failed: %s; motors remain stopped",
                 esp_err_to_name(idle_result));
        return;
    }

    const esp_err_t server_result = web_server_start();
    if (server_result != ESP_OK) {
        robot_stop();
        ESP_LOGE(TAG,
                 "Web server initialization failed: %s; motors remain stopped",
                 esp_err_to_name(server_result));
        return;
    }

    ESP_LOGI(TAG,
             "motors stopped; desktop idle enabled; waiting for Web control commands");
}
