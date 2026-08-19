#include "wifi_manager.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

static const char *TAG = "wifi_manager";

#define WIFI_CONNECTED_BIT BIT0

static EventGroupHandle_t s_event_group;
static portMUX_TYPE s_state_mux = portMUX_INITIALIZER_UNLOCKED;
static wifi_manager_snapshot_t s_state;
static int64_t s_connected_since_us;
static bool s_ever_connected;
static bool s_reconnect_pending;
static bool s_intentional_disconnect_pending;

static void connect_station(void)
{
    const esp_err_t result = esp_wifi_connect();
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(result));
    }
}

static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    (void)arg;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        connect_station();
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event = event_data;
        bool intentional = false;

        portENTER_CRITICAL(&s_state_mux);
        intentional = s_intentional_disconnect_pending;
        s_intentional_disconnect_pending = false;
        s_reconnect_pending = s_ever_connected;
        s_state.connected = false;
        s_state.ip.addr = 0U;
        ++s_state.disconnect_count;
        s_state.last_disconnect_reason = event != NULL ? event->reason : 0U;
        s_state.last_disconnect_intentional = intentional;
        portEXIT_CRITICAL(&s_state_mux);

        xEventGroupClearBits(s_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGW(TAG,
                 "wifi=DISCONNECTED reason=%u intentional=%s reconnect=requested",
                 event != NULL ? event->reason : 0U,
                 intentional ? "YES" : "NO");
        connect_station();
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = event_data;
        bool reconnected = false;

        portENTER_CRITICAL(&s_state_mux);
        reconnected = s_reconnect_pending;
        if (reconnected) {
            ++s_state.reconnect_count;
        }
        s_reconnect_pending = false;
        s_ever_connected = true;
        s_state.connected = true;
        s_state.ip = event->ip_info.ip;
        s_connected_since_us = esp_timer_get_time();
        portEXIT_CRITICAL(&s_state_mux);

        xEventGroupSetBits(s_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG,
                 "wifi=CONNECTED ip=" IPSTR " reconnected=%s",
                 IP2STR(&event->ip_info.ip),
                 reconnected ? "YES" : "NO");
    }
}

static esp_err_t initialize_nvs(void)
{
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES ||
        result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        result = nvs_flash_erase();
        if (result == ESP_OK) {
            result = nvs_flash_init();
        }
    }
    return result;
}

esp_err_t wifi_manager_init(void)
{
    const size_t ssid_length = strlen(CONFIG_ROBOT_WIFI_SSID);
    const size_t password_length = strlen(CONFIG_ROBOT_WIFI_PASSWORD);
    if (ssid_length == 0U || ssid_length >= sizeof(((wifi_config_t *)0)->sta.ssid) ||
        password_length >= sizeof(((wifi_config_t *)0)->sta.password)) {
        ESP_LOGE(TAG, "Wi-Fi credentials are missing or too long");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result = initialize_nvs();
    if (result != ESP_OK) {
        return result;
    }
    if ((result = esp_netif_init()) != ESP_OK ||
        (result = esp_event_loop_create_default()) != ESP_OK) {
        return result;
    }
    if (esp_netif_create_default_wifi_sta() == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_event_group = xEventGroupCreate();
    if (s_event_group == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    if ((result = esp_wifi_init(&init_config)) != ESP_OK ||
        (result = esp_event_handler_instance_register(
             WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL)) != ESP_OK ||
        (result = esp_event_handler_instance_register(
             IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL)) != ESP_OK) {
        return result;
    }

    wifi_config_t wifi_config = {0};
    memcpy(wifi_config.sta.ssid, CONFIG_ROBOT_WIFI_SSID, ssid_length + 1U);
    memcpy(wifi_config.sta.password, CONFIG_ROBOT_WIFI_PASSWORD, password_length + 1U);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    if ((result = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK ||
        (result = esp_wifi_set_config(WIFI_IF_STA, &wifi_config)) != ESP_OK ||
        (result = esp_wifi_start()) != ESP_OK ||
        (result = esp_wifi_set_ps(WIFI_PS_NONE)) != ESP_OK) {
        return result;
    }

    wifi_ps_type_t power_save = WIFI_PS_MIN_MODEM;
    if ((result = esp_wifi_get_ps(&power_save)) != ESP_OK ||
        power_save != WIFI_PS_NONE) {
        ESP_LOGE(TAG, "Wi-Fi power-save verification failed");
        return result == ESP_OK ? ESP_ERR_INVALID_STATE : result;
    }

    portENTER_CRITICAL(&s_state_mux);
    s_state.power_save = power_save;
    portEXIT_CRITICAL(&s_state_mux);
    ESP_LOGI(TAG, "Wi-Fi STA started; power_save=NONE");
    return ESP_OK;
}

bool wifi_manager_wait_for_ip(uint32_t timeout_ms)
{
    if (s_event_group == NULL) {
        return false;
    }
    const EventBits_t bits = xEventGroupWaitBits(
        s_event_group,
        WIFI_CONNECTED_BIT,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(timeout_ms));
    return (bits & WIFI_CONNECTED_BIT) != 0U;
}

wifi_manager_snapshot_t wifi_manager_get_snapshot(void)
{
    wifi_manager_snapshot_t snapshot;
    int64_t connected_since_us = 0;

    portENTER_CRITICAL(&s_state_mux);
    snapshot = s_state;
    connected_since_us = s_connected_since_us;
    portEXIT_CRITICAL(&s_state_mux);

    if (snapshot.connected && connected_since_us > 0) {
        snapshot.connected_since_seconds =
            (uint32_t)((esp_timer_get_time() - connected_since_us) / 1000000);
        wifi_ap_record_t access_point = {0};
        if (esp_wifi_sta_get_ap_info(&access_point) == ESP_OK) {
            snapshot.rssi = access_point.rssi;
            snapshot.rssi_valid = true;
        }
    }

    wifi_ps_type_t power_save = snapshot.power_save;
    if (esp_wifi_get_ps(&power_save) == ESP_OK) {
        snapshot.power_save = power_save;
    }
    return snapshot;
}

esp_err_t wifi_manager_disconnect_for_test(void)
{
    portENTER_CRITICAL(&s_state_mux);
    s_intentional_disconnect_pending = true;
    portEXIT_CRITICAL(&s_state_mux);

    const esp_err_t result = esp_wifi_disconnect();
    if (result != ESP_OK) {
        portENTER_CRITICAL(&s_state_mux);
        s_intentional_disconnect_pending = false;
        portEXIT_CRITICAL(&s_state_mux);
    }
    return result;
}
