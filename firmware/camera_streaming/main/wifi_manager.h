#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_netif_ip_addr.h"
#include "esp_wifi_types_generic.h"

typedef struct {
    bool connected;
    esp_ip4_addr_t ip;
    bool rssi_valid;
    int8_t rssi;
    uint32_t disconnect_count;
    uint32_t reconnect_count;
    uint32_t connected_since_seconds;
    wifi_ps_type_t power_save;
    uint8_t last_disconnect_reason;
    bool last_disconnect_intentional;
} wifi_manager_snapshot_t;

esp_err_t wifi_manager_init(void);
bool wifi_manager_wait_for_ip(uint32_t timeout_ms);
wifi_manager_snapshot_t wifi_manager_get_snapshot(void);
esp_err_t wifi_manager_disconnect_for_test(void);
