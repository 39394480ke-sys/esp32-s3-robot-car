#include "web_server.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif_ip_addr.h"

#include "oled_ui.h"
#include "robot_control.h"
#include "robot_status.h"
#include "servo_control.h"
#include "tts_control.h"
#include "web_page.h"
#include "wifi_manager.h"

static const char *TAG = "web_server";
static httpd_handle_t s_server;

static const char *motion_name(robot_motion_command_t motion)
{
    switch (motion) {
    case ROBOT_MOTION_FORWARD:
        return "FORWARD";
    case ROBOT_MOTION_BACKWARD:
        return "BACKWARD";
    case ROBOT_MOTION_TURN_LEFT:
        return "TURN_LEFT";
    case ROBOT_MOTION_TURN_RIGHT:
        return "TURN_RIGHT";
    case ROBOT_MOTION_STOP:
    default:
        return "STOP";
    }
}

static const char *stop_reason_name(robot_stop_reason_t reason)
{
    switch (reason) {
    case ROBOT_STOP_REASON_EXPLICIT:
        return "EXPLICIT";
    case ROBOT_STOP_REASON_ZERO_SPEED:
        return "ZERO_SPEED";
    case ROBOT_STOP_REASON_INVALID_COMMAND:
        return "INVALID_COMMAND";
    case ROBOT_STOP_REASON_WATCHDOG_TIMEOUT:
        return "WATCHDOG_TIMEOUT";
    case ROBOT_STOP_REASON_WIFI_DISCONNECTED:
        return "WIFI_DISCONNECTED";
    case ROBOT_STOP_REASON_DRIVER_ERROR:
        return "DRIVER_ERROR";
    case ROBOT_STOP_REASON_INIT_FAILURE:
        return "INIT_FAILURE";
    case ROBOT_STOP_REASON_ESTOP:
        return "ESTOP";
    case ROBOT_STOP_REASON_BOOT:
    default:
        return "BOOT";
    }
}

static esp_err_t send_json(httpd_req_t *request,
                           const char *status,
                           cJSON *body)
{
    char *serialized = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (serialized == NULL) {
        httpd_resp_set_status(request, "500 Internal Server Error");
        return httpd_resp_sendstr(request, "{\"error\":\"out of memory\"}");
    }

    httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    const esp_err_t result = httpd_resp_sendstr(request, serialized);
    cJSON_free(serialized);
    return result;
}

static esp_err_t send_result(httpd_req_t *request,
                             const char *status,
                             bool ok,
                             const char *message)
{
    cJSON *body = cJSON_CreateObject();
    if (body == NULL) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddBoolToObject(body, "ok", ok);
    if (message != NULL) {
        cJSON_AddStringToObject(body, ok ? "message" : "error", message);
    }
    return send_json(request, status, body);
}

static esp_err_t root_handler(httpd_req_t *request)
{
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request,
                           ROBOT_CONTROL_PAGE,
                           (ssize_t)ROBOT_CONTROL_PAGE_LENGTH);
}

static esp_err_t status_handler(httpd_req_t *request)
{
    const robot_status_snapshot_t state = robot_state_get_full_snapshot();
    char ip_address[16] = "0.0.0.0";
    if (state.wifi_connected) {
        snprintf(ip_address, sizeof(ip_address), IPSTR, IP2STR(&state.ip));
    }

    cJSON *body = cJSON_CreateObject();
    if (body == NULL) {
        return send_result(request,
                           "500 Internal Server Error",
                           false,
                           "out of memory");
    }
    cJSON_AddStringToObject(body, "robot_id", state.robot_id);
    cJSON_AddNumberToObject(body, "uptime", (double)state.uptime_seconds);
    cJSON_AddNumberToObject(body, "free_heap", state.free_heap_bytes);
    cJSON_AddNumberToObject(body,
                           "minimum_free_heap",
                           state.minimum_free_heap_bytes);
    cJSON_AddStringToObject(body,
                           "wifi",
                           state.wifi_connected ? "CONNECTED" : "DISCONNECTED");
    cJSON_AddBoolToObject(body, "wifi_connected", state.wifi_connected);
    cJSON_AddStringToObject(body, "ip", ip_address);
    if (state.rssi_valid) {
        cJSON_AddNumberToObject(body, "rssi", state.rssi);
    } else {
        cJSON_AddNullToObject(body, "rssi");
    }
    cJSON_AddNumberToObject(body, "wifi_disconnects", state.wifi_disconnects);
    cJSON_AddNumberToObject(body, "wifi_reconnects", state.wifi_reconnects);
    cJSON_AddNumberToObject(body,
                           "wifi_connected_seconds",
                           state.wifi_connected_seconds);
    cJSON_AddStringToObject(body, "motion", motion_name(state.motion_state));
    cJSON_AddNumberToObject(body, "speed", state.speed_percent);
    cJSON_AddBoolToObject(body, "watchdog_armed", state.watchdog_armed);
    cJSON_AddBoolToObject(body, "estop", state.estop);
    cJSON_AddStringToObject(body,
                           "last_stop_reason",
                           stop_reason_name(state.last_stop_reason));
    cJSON_AddNumberToObject(body, "yaw", state.yaw);
    cJSON_AddNumberToObject(body, "pitch", state.pitch);
    cJSON_AddNumberToObject(body, "yaw_min", state.yaw_limits.minimum);
    cJSON_AddNumberToObject(body, "yaw_center", state.yaw_limits.center);
    cJSON_AddNumberToObject(body, "yaw_max", state.yaw_limits.maximum);
    cJSON_AddNumberToObject(body, "pitch_min", state.pitch_limits.minimum);
    cJSON_AddNumberToObject(body, "pitch_center", state.pitch_limits.center);
    cJSON_AddNumberToObject(body, "pitch_max", state.pitch_limits.maximum);
    cJSON_AddBoolToObject(body, "oled_ready", state.oled_ready);
    cJSON_AddNumberToObject(body, "oled_address", state.oled_address);
    cJSON_AddStringToObject(body,
                           "expression",
                           oled_expression_name(state.oled_expression));
    cJSON_AddBoolToObject(body, "tts_ready", state.tts_ready);
    cJSON_AddBoolToObject(body, "tts_busy", state.tts_busy);
    cJSON_AddNumberToObject(body, "tts_requests", state.tts_requests);
    if (state.tts_has_last_phrase) {
        cJSON_AddStringToObject(body,
                               "tts_last_phrase",
                               tts_phrase_name(state.tts_last_phrase));
    } else {
        cJSON_AddNullToObject(body, "tts_last_phrase");
    }
    return send_json(request, "200 OK", body);
}

static bool parse_command(const char *value, robot_motion_command_t *command)
{
    if (strcmp(value, "forward") == 0) {
        *command = ROBOT_MOTION_FORWARD;
    } else if (strcmp(value, "backward") == 0) {
        *command = ROBOT_MOTION_BACKWARD;
    } else if (strcmp(value, "left") == 0) {
        *command = ROBOT_MOTION_TURN_LEFT;
    } else if (strcmp(value, "right") == 0) {
        *command = ROBOT_MOTION_TURN_RIGHT;
    } else {
        return false;
    }
    return true;
}

static esp_err_t drive_handler(httpd_req_t *request)
{
    char query[96];
    char command_value[16];
    char speed_value[8];
    const size_t query_length = httpd_req_get_url_query_len(request);
    if (query_length == 0U || query_length >= sizeof(query) ||
        httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query,
                              "command",
                              command_value,
                              sizeof(command_value)) != ESP_OK ||
        httpd_query_key_value(query,
                              "speed",
                              speed_value,
                              sizeof(speed_value)) != ESP_OK) {
        robot_drive((robot_motion_command_t)-1, 0U);
        return send_result(request, "400 Bad Request", false, "invalid query");
    }

    robot_motion_command_t command;
    char *end = NULL;
    errno = 0;
    const long speed = strtol(speed_value, &end, 10);
    if (!parse_command(command_value, &command) || errno != 0 ||
        end == speed_value || *end != '\0' || speed < 0L || speed > 100L) {
        robot_drive((robot_motion_command_t)-1, 0U);
        return send_result(request,
                           "400 Bad Request",
                           false,
                           "invalid command or speed");
    }

    const esp_err_t result = robot_drive(command, (uint8_t)speed);
    if (result == ESP_ERR_INVALID_STATE && robot_estop_is_active()) {
        return send_result(request,
                           "409 Conflict",
                           false,
                           "emergency stop active");
    }
    if (result != ESP_OK) {
        return send_result(request,
                           "500 Internal Server Error",
                           false,
                           "motor control failed");
    }
    return send_result(request, "200 OK", true, "drive accepted");
}

static esp_err_t stop_handler(httpd_req_t *request)
{
    robot_stop();
    return send_result(request, "200 OK", true, "stopped");
}

static esp_err_t estop_handler(httpd_req_t *request)
{
    robot_estop_activate();
    return send_result(request, "200 OK", true, "emergency stop active");
}

static esp_err_t clear_estop_handler(httpd_req_t *request)
{
    robot_estop_clear();
    return send_result(request, "200 OK", true, "emergency stop cleared");
}

static esp_err_t servo_handler(httpd_req_t *request)
{
    char query[64];
    char axis[8];
    char angle_value[8];
    const size_t query_length = httpd_req_get_url_query_len(request);
    if (query_length == 0U || query_length >= sizeof(query) ||
        httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "axis", axis, sizeof(axis)) != ESP_OK ||
        httpd_query_key_value(query,
                              "angle",
                              angle_value,
                              sizeof(angle_value)) != ESP_OK) {
        return send_result(request, "400 Bad Request", false, "invalid query");
    }

    char *end = NULL;
    errno = 0;
    const long angle = strtol(angle_value, &end, 10);
    if (errno != 0 || end == angle_value || *end != '\0' ||
        angle < INT16_MIN || angle > INT16_MAX) {
        return send_result(request, "400 Bad Request", false, "invalid angle");
    }

    esp_err_t result;
    if (strcmp(axis, "yaw") == 0) {
        result = servo_set_yaw((int16_t)angle);
    } else if (strcmp(axis, "pitch") == 0) {
        result = servo_set_pitch((int16_t)angle);
    } else {
        return send_result(request, "400 Bad Request", false, "invalid axis");
    }
    if (result != ESP_OK) {
        return send_result(request,
                           "500 Internal Server Error",
                           false,
                           "gimbal control failed");
    }

    const servo_control_snapshot_t servo = servo_control_get_snapshot();
    cJSON *body = cJSON_CreateObject();
    if (body == NULL) {
        return send_result(request,
                           "500 Internal Server Error",
                           false,
                           "out of memory");
    }
    cJSON_AddBoolToObject(body, "ok", true);
    cJSON_AddNumberToObject(body, "yaw", servo.yaw);
    cJSON_AddNumberToObject(body, "pitch", servo.pitch);
    return send_json(request, "200 OK", body);
}

static esp_err_t oled_handler(httpd_req_t *request)
{
    char query[48];
    char expression[16];
    const size_t query_length = httpd_req_get_url_query_len(request);
    if (query_length == 0U || query_length >= sizeof(query) ||
        httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query,
                              "expression",
                              expression,
                              sizeof(expression)) != ESP_OK) {
        return send_result(request, "400 Bad Request", false, "invalid query");
    }

    const esp_err_t result = oled_set_expression(expression);
    if (result == ESP_ERR_INVALID_ARG) {
        return send_result(request,
                           "400 Bad Request",
                           false,
                           "invalid expression");
    }
    if (result != ESP_OK) {
        return send_result(request,
                           "500 Internal Server Error",
                           false,
                           "OLED render failed");
    }
    return send_result(request, "200 OK", true, expression);
}

static esp_err_t tts_handler(httpd_req_t *request)
{
    char query[40];
    char phrase_name[16];
    const size_t query_length = httpd_req_get_url_query_len(request);
    if (query_length == 0U || query_length >= sizeof(query) ||
        httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query,
                              "phrase",
                              phrase_name,
                              sizeof(phrase_name)) != ESP_OK) {
        return send_result(request, "400 Bad Request", false, "invalid query");
    }

    tts_phrase_id_t phrase;
    if (!tts_phrase_from_name(phrase_name, &phrase)) {
        return send_result(request, "400 Bad Request", false, "invalid phrase");
    }
    const esp_err_t result = tts_play_phrase(phrase);
    if (result == ESP_ERR_INVALID_STATE) {
        return send_result(request,
                           "503 Service Unavailable",
                           false,
                           "TTS unavailable");
    }
    if (result != ESP_OK) {
        return send_result(request,
                           "500 Internal Server Error",
                           false,
                           "TTS playback failed");
    }
    return send_result(request, "200 OK", true, phrase_name);
}

static esp_err_t register_uri(const httpd_uri_t *uri)
{
    const esp_err_t result = httpd_register_uri_handler(s_server, uri);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "failed to register %s: %s", uri->uri, esp_err_to_name(result));
    }
    return result;
}

esp_err_t web_server_start(void)
{
    if (s_server != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 9U;
    config.lru_purge_enable = true;
    esp_err_t result = httpd_start(&s_server, &config);
    if (result != ESP_OK) {
        return result;
    }

    const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = root_handler},
        {.uri = "/status", .method = HTTP_GET, .handler = status_handler},
        {.uri = "/api/drive", .method = HTTP_POST, .handler = drive_handler},
        {.uri = "/api/stop", .method = HTTP_POST, .handler = stop_handler},
        {.uri = "/api/estop", .method = HTTP_POST, .handler = estop_handler},
        {.uri = "/api/estop/clear",
         .method = HTTP_POST,
         .handler = clear_estop_handler},
        {.uri = "/api/servo", .method = HTTP_POST, .handler = servo_handler},
        {.uri = "/api/oled", .method = HTTP_POST, .handler = oled_handler},
        {.uri = "/api/tts", .method = HTTP_POST, .handler = tts_handler},
    };

    for (size_t index = 0U; index < sizeof(routes) / sizeof(routes[0]); ++index) {
        result = register_uri(&routes[index]);
        if (result != ESP_OK) {
            httpd_stop(s_server);
            s_server = NULL;
            return result;
        }
    }

    ESP_LOGI(TAG, "HTTP control server started on port %u", config.server_port);
    return ESP_OK;
}
