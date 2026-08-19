#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif_ip_addr.h"

#include "camera_init.h"
#include "camera_safety_check.h"
#include "hardware_info.h"
#include "stream_server.h"
#include "wifi_manager.h"

static const char *TAG = "camera_streaming";

static const char *power_save_name(wifi_ps_type_t power_save)
{
    return power_save == WIFI_PS_NONE ? "NONE" : "UNEXPECTED";
}

static void log_status(void)
{
    const wifi_manager_snapshot_t status = wifi_manager_get_snapshot();
    const stream_server_snapshot_t stream = stream_server_get_snapshot();
    const unsigned internal_free = (unsigned)heap_caps_get_free_size(
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const unsigned psram_free = (unsigned)heap_caps_get_free_size(
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (status.connected) {
        ESP_LOGI(TAG,
                 "robot_id=%s wifi=CONNECTED ip=" IPSTR
                 " rssi=%d disconnect_count=%" PRIu32
                 " reconnect_count=%" PRIu32
                 " connected_since=%" PRIu32 "s power_save=%s "
                 "internal_free=%u psram_free=%u",
                 CONFIG_ROBOT_ID,
                 IP2STR(&status.ip),
                 status.rssi_valid ? status.rssi : 0,
                 status.disconnect_count,
                 status.reconnect_count,
                 status.connected_since_seconds,
                 power_save_name(status.power_save),
                 internal_free,
                 psram_free);
    } else {
        ESP_LOGI(TAG,
                 "robot_id=%s wifi=DISCONNECTED ip=0.0.0.0 rssi=NA "
                 "disconnect_count=%" PRIu32 " reconnect_count=%" PRIu32
                 " connected_since=0s power_save=%s internal_free=%u psram_free=%u",
                 CONFIG_ROBOT_ID,
                 status.disconnect_count,
                 status.reconnect_count,
                 power_save_name(status.power_save),
                 internal_free,
                 psram_free);
    }

    ESP_LOGI(TAG,
             "stream_session=%" PRIu64 " active=%s camera_frames=%" PRIu64
             " camera_fps=%.3f frames_sent=%" PRIu64 " stream_fps=%.3f "
             "throughput_bps=%.0f avg_send_ms=%.3f p95_send_ms=%.3f "
             "invalid=%" PRIu64 " header_mismatch=%" PRIu64
             " send_failures=%" PRIu64,
             stream.metrics.session_id,
             stream.metrics.session_active ? "YES" : "NO",
             stream.metrics.camera_frames,
             stream.metrics.camera_fps,
             stream.metrics.frames_sent,
             stream.metrics.stream_fps,
             stream.metrics.throughput_bps,
             stream.metrics.average_send_ms,
             stream.metrics.p95_send_ms,
             stream.metrics.invalid_frames,
             stream.metrics.header_mismatch_frames,
             stream.metrics.send_failures);
}

static bool run_camera_bringup(void)
{
    if (!hardware_info_run_probe() ||
        !camera_run_single_frame_safety_check() ||
        !camera_run_continuous_safety_check()) {
        return false;
    }
    return camera_init_baseline() == ESP_OK;
}

static void run_reconnect_self_test(void)
{
#if CONFIG_D2_WIFI_RECONNECT_SELF_TEST
    ESP_LOGI(TAG, "controlled reconnect test scheduled in 30 seconds");
    vTaskDelay(pdMS_TO_TICKS(30000));

    const wifi_manager_snapshot_t before = wifi_manager_get_snapshot();
    ESP_LOGI(TAG, "controlled reconnect test: disconnect=requested");
    if (wifi_manager_disconnect_for_test() != ESP_OK) {
        ESP_LOGE(TAG, "D2_WIFI_RECONNECT_SELF_TEST=FAIL disconnect call failed");
        return;
    }

    bool reconnected = false;
    for (unsigned attempt = 0U; attempt < 300U; ++attempt) {
        const wifi_manager_snapshot_t current = wifi_manager_get_snapshot();
        if (current.connected &&
            current.reconnect_count > before.reconnect_count) {
            reconnected = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    ESP_LOGI(TAG,
             "D2_WIFI_RECONNECT_SELF_TEST=%s",
             reconnected ? "PASS" : "FAIL");
#endif
}

void app_main(void)
{
    ESP_LOGI(TAG, "D2 Camera + Wi-Fi STA bring-up start");
    if (!run_camera_bringup()) {
        ESP_LOGE(TAG, "Camera bring-up failed; Wi-Fi initialization skipped");
        return;
    }
    ESP_LOGI(TAG, "Camera baseline remains initialized");

    const esp_err_t wifi_result = wifi_manager_init();
    if (wifi_result != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi initialization failed: %s", esp_err_to_name(wifi_result));
        camera_shutdown();
        return;
    }

    const esp_err_t server_result = stream_server_start();
    ESP_LOGI(TAG,
             "D2_HTTP_SERVER=%s result=%s",
             server_result == ESP_OK ? "PASS" : "FAIL",
             esp_err_to_name(server_result));

    const bool connected = wifi_manager_wait_for_ip(30000U);
    ESP_LOGI(TAG,
             "D2_WIFI_INITIAL_CONNECTION=%s",
             connected ? "PASS" : "TIMEOUT_RETRYING");
    if (connected) {
        run_reconnect_self_test();
    }

    while (true) {
        log_status();
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
