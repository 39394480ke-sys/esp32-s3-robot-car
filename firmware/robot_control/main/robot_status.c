#include "robot_status.h"

#include "esp_timer.h"
#include "esp_system.h"

#include "desktop_idle.h"
#include "oled_ui.h"
#include "robot_state.h"
#include "servo_control.h"
#include "tts_control.h"
#include "wifi_manager.h"

robot_status_snapshot_t robot_state_get_full_snapshot(void)
{
    const wifi_manager_snapshot_t wifi = wifi_manager_get_snapshot();
    const robot_state_snapshot_t robot = robot_state_get_snapshot();
    const desktop_idle_snapshot_t idle = desktop_idle_get_snapshot();
    const servo_control_snapshot_t servo = servo_control_get_snapshot();
    const oled_ui_snapshot_t oled = oled_ui_get_snapshot();
    const tts_control_snapshot_t tts = tts_control_get_snapshot();

    return (robot_status_snapshot_t){
        .robot_id = ROBOT_ID,
        .uptime_seconds = (uint64_t)(esp_timer_get_time() / 1000000),
        .free_heap_bytes = esp_get_free_heap_size(),
        .minimum_free_heap_bytes = esp_get_minimum_free_heap_size(),
        .wifi_connected = wifi.connected,
        .ip = wifi.ip,
        .rssi_valid = wifi.rssi_valid,
        .rssi = wifi.rssi,
        .wifi_disconnects = wifi.disconnect_count,
        .wifi_reconnects = wifi.reconnect_count,
        .wifi_connected_seconds = wifi.connected_since_seconds,
        .motion_state = robot.motion,
        .speed_percent = robot.speed_percent,
        .watchdog_armed = robot.watchdog_armed,
        .estop = robot.estop,
        .last_stop_reason = robot.last_stop_reason,
        .mode = idle.mode,
        .idle_enabled = idle.enabled,
        .idle_action = idle.action,
        .yaw = servo.yaw,
        .pitch = servo.pitch,
        .yaw_limits = servo.yaw_limits,
        .pitch_limits = servo.pitch_limits,
        .oled_ready = oled.initialized,
        .oled_address = oled.i2c_address,
        .oled_expression = oled.expression,
        .tts_ready = tts.initialized,
        .tts_busy = tts.busy,
        .tts_has_last_phrase = tts.has_last_phrase,
        .tts_last_phrase = tts.last_phrase,
        .tts_requests = tts.request_count,
    };
}
