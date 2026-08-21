#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_netif_ip_addr.h"

#include "desktop_idle.h"
#include "oled_expression.h"
#include "robot_types.h"
#include "servo_policy.h"
#include "tts_protocol.h"

#define ROBOT_ID "esp32-s3-robot-car"

typedef struct {
    const char *robot_id;
    uint64_t uptime_seconds;
    uint32_t free_heap_bytes;
    uint32_t minimum_free_heap_bytes;

    bool wifi_connected;
    esp_ip4_addr_t ip;
    bool rssi_valid;
    int8_t rssi;
    uint32_t wifi_disconnects;
    uint32_t wifi_reconnects;
    uint32_t wifi_connected_seconds;

    robot_motion_command_t motion_state;
    uint8_t speed_percent;
    bool watchdog_armed;
    bool estop;
    robot_stop_reason_t last_stop_reason;
    robot_mode_t mode;
    bool idle_enabled;
    desktop_idle_action_t idle_action;

    int16_t yaw;
    int16_t pitch;
    servo_axis_limits_t yaw_limits;
    servo_axis_limits_t pitch_limits;

    bool oled_ready;
    uint8_t oled_address;
    oled_expression_id_t oled_expression;

    bool tts_ready;
    bool tts_busy;
    bool tts_has_last_phrase;
    tts_phrase_id_t tts_last_phrase;
    uint32_t tts_requests;
} robot_status_snapshot_t;

robot_status_snapshot_t robot_state_get_full_snapshot(void);
