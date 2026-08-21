#include "desktop_idle.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "desktop_idle_policy.h"
#include "oled_ui.h"
#include "robot_control.h"
#include "robot_state.h"
#include "servo_control.h"

#define DESKTOP_IDLE_POLL_MS 100U
#define DESKTOP_IDLE_TASK_STACK_SIZE 4096U
#define DESKTOP_IDLE_TASK_PRIORITY 4U

static const char *TAG = "desktop_idle";
static const char *const IDLE_EXPRESSIONS[] = {
    "idle",
    "curious",
    "sleepy",
    "watching",
};

static SemaphoreHandle_t s_mutex;
static desktop_idle_snapshot_t s_state;
static uint64_t s_last_activity_ms;
static uint64_t s_next_gesture_ms;
static uint64_t s_gesture_end_ms;
static uint64_t s_next_expression_ms;

static uint64_t now_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000);
}

static uint64_t random_deadline(uint64_t current_ms,
                                uint32_t minimum_ms,
                                uint32_t maximum_ms)
{
    return current_ms + desktop_idle_policy_random_delay(esp_random(),
                                                          minimum_ms,
                                                          maximum_ms);
}

static void center_gimbal_locked(void)
{
    const esp_err_t result = look_center();
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "failed to center idle gimbal: %s", esp_err_to_name(result));
    }
}

static void enter_manual_locked(uint64_t current_ms, bool reset_activity)
{
    const bool was_idle = s_state.mode == ROBOT_MODE_DESKTOP_IDLE ||
                          s_state.action != DESKTOP_IDLE_ACTION_NONE;
    s_state.mode = ROBOT_MODE_MANUAL;
    s_state.action = DESKTOP_IDLE_ACTION_NONE;
    s_gesture_end_ms = 0U;
    if (reset_activity) {
        s_last_activity_ms = current_ms;
    }
    if (was_idle) {
        center_gimbal_locked();
        ESP_LOGI(TAG, "idle cancelled; mode=MANUAL");
    }
}

static void cancel_for_estop_locked(uint64_t current_ms)
{
    const bool was_idle = s_state.mode == ROBOT_MODE_DESKTOP_IDLE ||
                          s_state.action != DESKTOP_IDLE_ACTION_NONE;
    s_state.mode = ROBOT_MODE_MANUAL;
    s_state.action = DESKTOP_IDLE_ACTION_NONE;
    s_gesture_end_ms = 0U;
    s_last_activity_ms = current_ms;
    if (was_idle) {
        ESP_LOGI(TAG, "idle cancelled by E-stop; servo position held");
    }
}

static void enter_idle_locked(uint64_t current_ms)
{
    robot_stop();
    s_state.mode = ROBOT_MODE_DESKTOP_IDLE;
    s_state.action = DESKTOP_IDLE_ACTION_NONE;
    s_next_gesture_ms = random_deadline(current_ms,
                                        DESKTOP_IDLE_GESTURE_MIN_DELAY_MS,
                                        DESKTOP_IDLE_GESTURE_MAX_DELAY_MS);
    s_next_expression_ms = random_deadline(
        current_ms,
        DESKTOP_IDLE_EXPRESSION_MIN_DELAY_MS,
        DESKTOP_IDLE_EXPRESSION_MAX_DELAY_MS);
    const esp_err_t result = oled_set_expression("idle");
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "failed to set entry expression: %s", esp_err_to_name(result));
    }
    ESP_LOGI(TAG, "mode=DESKTOP_IDLE; motors stopped");
}

static void start_gesture_locked(uint64_t current_ms)
{
    const servo_control_snapshot_t servo = servo_control_get_snapshot();
    const desktop_idle_action_t action =
        (desktop_idle_action_t)(DESKTOP_IDLE_ACTION_LOOK_LEFT +
                                esp_random() % 3U);
    esp_err_t result = ESP_OK;
    switch (action) {
    case DESKTOP_IDLE_ACTION_LOOK_LEFT:
        result = servo_set_yaw(desktop_idle_policy_offset_angle(
            &servo.yaw_limits, -DESKTOP_IDLE_YAW_OFFSET_DEGREES));
        break;
    case DESKTOP_IDLE_ACTION_LOOK_RIGHT:
        result = servo_set_yaw(desktop_idle_policy_offset_angle(
            &servo.yaw_limits, DESKTOP_IDLE_YAW_OFFSET_DEGREES));
        break;
    case DESKTOP_IDLE_ACTION_LOOK_UP:
        result = servo_set_pitch(desktop_idle_policy_offset_angle(
            &servo.pitch_limits, -DESKTOP_IDLE_PITCH_OFFSET_DEGREES));
        break;
    case DESKTOP_IDLE_ACTION_NONE:
    default:
        return;
    }
    if (result == ESP_OK) {
        s_state.action = action;
        s_gesture_end_ms = current_ms + DESKTOP_IDLE_GESTURE_HOLD_MS;
        ESP_LOGI(TAG, "idle action=%s", desktop_idle_action_name(action));
    } else {
        ESP_LOGW(TAG, "idle gesture failed: %s", esp_err_to_name(result));
        s_next_gesture_ms = random_deadline(current_ms,
                                            DESKTOP_IDLE_GESTURE_MIN_DELAY_MS,
                                            DESKTOP_IDLE_GESTURE_MAX_DELAY_MS);
    }
}

static void finish_gesture_locked(uint64_t current_ms)
{
    center_gimbal_locked();
    s_state.action = DESKTOP_IDLE_ACTION_NONE;
    s_gesture_end_ms = 0U;
    s_next_gesture_ms = random_deadline(current_ms,
                                        DESKTOP_IDLE_GESTURE_MIN_DELAY_MS,
                                        DESKTOP_IDLE_GESTURE_MAX_DELAY_MS);
}

static void update_expression_locked(uint64_t current_ms)
{
    const size_t count = sizeof(IDLE_EXPRESSIONS) / sizeof(IDLE_EXPRESSIONS[0]);
    size_t index = esp_random() % count;
    const oled_ui_snapshot_t oled = oled_ui_get_snapshot();
    if (strcmp(IDLE_EXPRESSIONS[index],
               oled_expression_name(oled.expression)) == 0) {
        index = (index + 1U) % count;
    }
    const char *expression = IDLE_EXPRESSIONS[index];
    const esp_err_t result = oled_set_expression(expression);
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "idle expression failed: %s", esp_err_to_name(result));
    }
    s_next_expression_ms = random_deadline(
        current_ms,
        DESKTOP_IDLE_EXPRESSION_MIN_DELAY_MS,
        DESKTOP_IDLE_EXPRESSION_MAX_DELAY_MS);
}

static void desktop_idle_task(void *context)
{
    (void)context;
    while (true) {
        const uint64_t current_ms = now_ms();
        const robot_state_snapshot_t robot = robot_state_get_snapshot();

        if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
            if (robot.estop) {
                cancel_for_estop_locked(current_ms);
            } else if (!s_state.enabled) {
                enter_manual_locked(current_ms, false);
            } else if (s_state.mode == ROBOT_MODE_MANUAL) {
                if (desktop_idle_policy_should_enter(
                        s_state.enabled,
                        robot.estop,
                        robot.motion == ROBOT_MOTION_STOP,
                        current_ms,
                        s_last_activity_ms)) {
                    enter_idle_locked(current_ms);
                }
            } else if (robot.motion != ROBOT_MOTION_STOP) {
                robot_stop();
            } else if (s_state.action != DESKTOP_IDLE_ACTION_NONE &&
                       current_ms >= s_gesture_end_ms) {
                finish_gesture_locked(current_ms);
            } else if (s_state.action == DESKTOP_IDLE_ACTION_NONE &&
                       current_ms >= s_next_gesture_ms) {
                start_gesture_locked(current_ms);
            }

            if (s_state.mode == ROBOT_MODE_DESKTOP_IDLE &&
                !robot.estop && current_ms >= s_next_expression_ms) {
                update_expression_locked(current_ms);
            }
            xSemaphoreGive(s_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(DESKTOP_IDLE_POLL_MS));
    }
}

esp_err_t desktop_idle_init(void)
{
    if (s_mutex != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_state = (desktop_idle_snapshot_t){
        .initialized = true,
        .enabled = true,
        .mode = ROBOT_MODE_MANUAL,
        .action = DESKTOP_IDLE_ACTION_NONE,
    };
    s_last_activity_ms = now_ms();
    if (xTaskCreate(desktop_idle_task,
                    "desktop_idle",
                    DESKTOP_IDLE_TASK_STACK_SIZE,
                    NULL,
                    DESKTOP_IDLE_TASK_PRIORITY,
                    NULL) != pdPASS) {
        s_state.initialized = false;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "ready: enabled=true entry_delay=%ums", DESKTOP_IDLE_ENTRY_DELAY_MS);
    return ESP_OK;
}

esp_err_t desktop_idle_set_enabled(bool enabled)
{
    if (s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    const uint64_t current_ms = now_ms();
    enter_manual_locked(current_ms, true);
    s_state.enabled = enabled;
    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "enabled=%s", enabled ? "true" : "false");
    return ESP_OK;
}

void desktop_idle_record_user_activity(void)
{
    if (s_mutex == NULL) {
        return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    enter_manual_locked(now_ms(), true);
    xSemaphoreGive(s_mutex);
}

void desktop_idle_cancel_for_estop(void)
{
    if (s_mutex == NULL) {
        return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    cancel_for_estop_locked(now_ms());
    xSemaphoreGive(s_mutex);
}

desktop_idle_snapshot_t desktop_idle_get_snapshot(void)
{
    desktop_idle_snapshot_t snapshot = {
        .mode = ROBOT_MODE_MANUAL,
        .action = DESKTOP_IDLE_ACTION_NONE,
    };
    if (s_mutex != NULL && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        snapshot = s_state;
        xSemaphoreGive(s_mutex);
    }
    return snapshot;
}

const char *robot_mode_name(robot_mode_t mode)
{
    return mode == ROBOT_MODE_DESKTOP_IDLE ? "DESKTOP_IDLE" : "MANUAL";
}

const char *desktop_idle_action_name(desktop_idle_action_t action)
{
    switch (action) {
    case DESKTOP_IDLE_ACTION_LOOK_LEFT:
        return "LOOK_LEFT";
    case DESKTOP_IDLE_ACTION_LOOK_RIGHT:
        return "LOOK_RIGHT";
    case DESKTOP_IDLE_ACTION_LOOK_UP:
        return "LOOK_UP";
    case DESKTOP_IDLE_ACTION_NONE:
    default:
        return "NONE";
    }
}
