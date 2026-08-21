#include "robot_control.h"

#include <stdbool.h>
#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "motor_control.h"
#include "robot_control_internal.h"
#include "robot_state.h"
#include "safety.h"

static const char *TAG = "robot_control";

static SemaphoreHandle_t s_control_mutex;
static bool s_initialized;
static bool s_watchdog_armed;
static bool s_estop_active;
static uint64_t s_last_command_ms;

static bool command_is_valid(robot_motion_command_t command)
{
    return command >= ROBOT_MOTION_STOP &&
           command <= ROBOT_MOTION_TURN_RIGHT;
}

static robot_stop_reason_t stop_locked(robot_stop_reason_t reason)
{
    s_watchdog_armed = false;
    const esp_err_t result = motor_control_stop();
    const robot_stop_reason_t recorded_reason =
        result == ESP_OK ? reason : ROBOT_STOP_REASON_DRIVER_ERROR;
    robot_state_record_stop(recorded_reason);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "motor stop failed: %s", esp_err_to_name(result));
    }
    return recorded_reason;
}

static void stop_with_reason(robot_stop_reason_t reason)
{
    if (s_control_mutex == NULL) {
        motor_control_stop();
        robot_state_record_stop(reason);
        return;
    }

    xSemaphoreTake(s_control_mutex, portMAX_DELAY);
    stop_locked(reason);
    xSemaphoreGive(s_control_mutex);
}

void robot_control_watchdog_poll(uint64_t now_ms, void *context)
{
    (void)context;
    if (s_control_mutex == NULL) {
        return;
    }

    xSemaphoreTake(s_control_mutex, portMAX_DELAY);
    if (s_initialized &&
        safety_watchdog_expired(s_watchdog_armed,
                                s_last_command_ms,
                                now_ms,
                                ROBOT_CONTROL_WATCHDOG_TIMEOUT_MS)) {
        ESP_LOGW(TAG, "control watchdog expired; stopping motors");
        stop_locked(ROBOT_STOP_REASON_WATCHDOG_TIMEOUT);
    }
    xSemaphoreGive(s_control_mutex);
}

esp_err_t robot_control_init(void)
{
    if (s_control_mutex != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    robot_state_init();
    s_control_mutex = xSemaphoreCreateMutex();
    if (s_control_mutex == NULL) {
        motor_control_stop();
        robot_state_record_stop(ROBOT_STOP_REASON_INIT_FAILURE);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t result = motor_control_init();
    if (result != ESP_OK) {
        stop_with_reason(ROBOT_STOP_REASON_INIT_FAILURE);
        return result;
    }

    s_watchdog_armed = false;
    s_estop_active = false;
    s_last_command_ms = 0U;
    s_initialized = true;
    result = safety_start_polling(robot_control_watchdog_poll, NULL);
    if (result != ESP_OK) {
        xSemaphoreTake(s_control_mutex, portMAX_DELAY);
        s_initialized = false;
        stop_locked(ROBOT_STOP_REASON_INIT_FAILURE);
        xSemaphoreGive(s_control_mutex);
        return result;
    }

    ESP_LOGI(TAG,
             "ready: default_speed=%u%% watchdog=%ums",
             ROBOT_DEFAULT_SPEED_PERCENT,
             ROBOT_CONTROL_WATCHDOG_TIMEOUT_MS);
    return ESP_OK;
}

esp_err_t robot_drive(robot_motion_command_t command, uint8_t speed_percent)
{
    if (!command_is_valid(command) || speed_percent > 100U) {
        stop_with_reason(ROBOT_STOP_REASON_INVALID_COMMAND);
        return ESP_ERR_INVALID_ARG;
    }
    if (command == ROBOT_MOTION_STOP) {
        robot_stop();
        return ESP_OK;
    }
    if (speed_percent == 0U) {
        stop_with_reason(ROBOT_STOP_REASON_ZERO_SPEED);
        return ESP_OK;
    }
    if (s_control_mutex == NULL) {
        robot_state_record_stop(ROBOT_STOP_REASON_INIT_FAILURE);
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_control_mutex, portMAX_DELAY);
    if (!s_initialized) {
        stop_locked(ROBOT_STOP_REASON_INIT_FAILURE);
        xSemaphoreGive(s_control_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    if (s_estop_active) {
        stop_locked(ROBOT_STOP_REASON_ESTOP);
        robot_state_set_estop(true);
        xSemaphoreGive(s_control_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t result = motor_control_apply(command, speed_percent);
    if (result != ESP_OK) {
        stop_locked(ROBOT_STOP_REASON_DRIVER_ERROR);
        xSemaphoreGive(s_control_mutex);
        return result;
    }

    s_last_command_ms = (uint64_t)(esp_timer_get_time() / 1000);
    s_watchdog_armed = true;
    robot_state_record_motion(command, speed_percent, true);
    xSemaphoreGive(s_control_mutex);
    return ESP_OK;
}

esp_err_t robot_move_forward(uint8_t speed_percent)
{
    return robot_drive(ROBOT_MOTION_FORWARD, speed_percent);
}

esp_err_t robot_move_backward(uint8_t speed_percent)
{
    return robot_drive(ROBOT_MOTION_BACKWARD, speed_percent);
}

esp_err_t robot_turn_left(uint8_t speed_percent)
{
    return robot_drive(ROBOT_MOTION_TURN_LEFT, speed_percent);
}

esp_err_t robot_turn_right(uint8_t speed_percent)
{
    return robot_drive(ROBOT_MOTION_TURN_RIGHT, speed_percent);
}

void robot_stop(void)
{
    stop_with_reason(ROBOT_STOP_REASON_EXPLICIT);
}

void robot_notify_wifi_disconnected(void)
{
    stop_with_reason(ROBOT_STOP_REASON_WIFI_DISCONNECTED);
}

void robot_estop_activate(void)
{
    if (s_control_mutex == NULL) {
        motor_control_stop();
        s_estop_active = true;
        robot_state_record_stop(ROBOT_STOP_REASON_ESTOP);
        robot_state_set_estop(true);
        return;
    }

    xSemaphoreTake(s_control_mutex, portMAX_DELAY);
    s_estop_active = true;
    stop_locked(ROBOT_STOP_REASON_ESTOP);
    robot_state_set_estop(true);
    xSemaphoreGive(s_control_mutex);
}

void robot_estop_clear(void)
{
    if (s_control_mutex == NULL) {
        return;
    }

    xSemaphoreTake(s_control_mutex, portMAX_DELAY);
    if (s_initialized) {
        stop_locked(ROBOT_STOP_REASON_ESTOP);
        s_estop_active = false;
        robot_state_set_estop(false);
    }
    xSemaphoreGive(s_control_mutex);
}

bool robot_estop_is_active(void)
{
    if (s_control_mutex == NULL) {
        return s_estop_active;
    }

    xSemaphoreTake(s_control_mutex, portMAX_DELAY);
    const bool active = s_estop_active;
    xSemaphoreGive(s_control_mutex);
    return active;
}
