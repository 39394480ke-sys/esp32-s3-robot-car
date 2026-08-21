#include "servo_control.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#ifndef CONFIG_ROBOT_YAW_INVERTED
#define CONFIG_ROBOT_YAW_INVERTED 0
#endif

#ifndef CONFIG_ROBOT_YAW_TRIM_DEGREES
#define CONFIG_ROBOT_YAW_TRIM_DEGREES 0
#endif

#ifndef CONFIG_ROBOT_PITCH_INVERTED
#define CONFIG_ROBOT_PITCH_INVERTED 0
#endif

#define YAW_GPIO GPIO_NUM_1
#define PITCH_GPIO GPIO_NUM_2
#define SERVO_PWM_FREQUENCY_HZ 50
#define SERVO_PWM_MAX_DUTY ((1U << 14U) - 1U)
#define GESTURE_DELAY_MS 220U

static const ledc_mode_t SERVO_PWM_MODE = LEDC_LOW_SPEED_MODE;
static const ledc_timer_t SERVO_PWM_TIMER = LEDC_TIMER_1;
static const ledc_channel_t YAW_PWM_CHANNEL = LEDC_CHANNEL_2;
static const ledc_channel_t PITCH_PWM_CHANNEL = LEDC_CHANNEL_3;

static const servo_axis_limits_t YAW_LIMITS = {
    .minimum = CONFIG_ROBOT_YAW_MIN_DEGREES,
    .center = CONFIG_ROBOT_YAW_CENTER_DEGREES,
    .maximum = CONFIG_ROBOT_YAW_MAX_DEGREES,
};
static const servo_axis_limits_t PITCH_LIMITS = {
    .minimum = CONFIG_ROBOT_PITCH_MIN_DEGREES,
    .center = CONFIG_ROBOT_PITCH_CENTER_DEGREES,
    .maximum = CONFIG_ROBOT_PITCH_MAX_DEGREES,
};

static SemaphoreHandle_t s_mutex;
static servo_control_snapshot_t s_state;

static esp_err_t set_axis_locked(ledc_channel_t channel,
                                 int16_t requested_angle,
                                 const servo_axis_limits_t *limits,
                                 int16_t trim,
                                 bool inverted,
                                 int16_t *stored_angle)
{
    const int16_t angle = servo_policy_clamp(requested_angle, limits);
    const int16_t calibrated_angle = servo_policy_apply_trim(angle, trim);
    const uint32_t duty =
        servo_policy_angle_to_duty(calibrated_angle, inverted, SERVO_PWM_MAX_DUTY);
    esp_err_t result = ledc_set_duty(SERVO_PWM_MODE, channel, duty);
    if (result == ESP_OK) {
        result = ledc_update_duty(SERVO_PWM_MODE, channel);
    }
    if (result == ESP_OK) {
        *stored_angle = angle;
    }
    return result;
}

static esp_err_t set_axis(ledc_channel_t channel,
                          int16_t requested_angle,
                          const servo_axis_limits_t *limits,
                          int16_t trim,
                          bool inverted,
                          int16_t *stored_angle)
{
    if (!s_state.initialized || s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    const esp_err_t result = set_axis_locked(channel,
                                             requested_angle,
                                             limits,
                                             trim,
                                             inverted,
                                             stored_angle);
    xSemaphoreGive(s_mutex);
    return result;
}

esp_err_t servo_control_init(void)
{
    if (YAW_LIMITS.minimum > YAW_LIMITS.center ||
        YAW_LIMITS.center > YAW_LIMITS.maximum ||
        PITCH_LIMITS.minimum > PITCH_LIMITS.center ||
        PITCH_LIMITS.center > PITCH_LIMITS.maximum) {
        return ESP_ERR_INVALID_ARG;
    }
    s_state = (servo_control_snapshot_t){
        .yaw = YAW_LIMITS.center,
        .pitch = PITCH_LIMITS.center,
        .yaw_limits = YAW_LIMITS,
        .pitch_limits = PITCH_LIMITS,
    };
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const ledc_timer_config_t timer = {
        .speed_mode = SERVO_PWM_MODE,
        .duty_resolution = LEDC_TIMER_14_BIT,
        .timer_num = SERVO_PWM_TIMER,
        .freq_hz = SERVO_PWM_FREQUENCY_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
        .deconfigure = false,
    };
    esp_err_t result = ledc_timer_config(&timer);
    if (result != ESP_OK) {
        return result;
    }

    const ledc_channel_config_t yaw_channel = {
        .gpio_num = YAW_GPIO,
        .speed_mode = SERVO_PWM_MODE,
        .channel = YAW_PWM_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = SERVO_PWM_TIMER,
        .duty = servo_policy_angle_to_duty(
            servo_policy_apply_trim(YAW_LIMITS.center,
                                    CONFIG_ROBOT_YAW_TRIM_DEGREES),
            CONFIG_ROBOT_YAW_INVERTED,
            SERVO_PWM_MAX_DUTY),
        .hpoint = 0,
        .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
        .flags.output_invert = 0,
    };
    const ledc_channel_config_t pitch_channel = {
        .gpio_num = PITCH_GPIO,
        .speed_mode = SERVO_PWM_MODE,
        .channel = PITCH_PWM_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = SERVO_PWM_TIMER,
        .duty = servo_policy_angle_to_duty(
            PITCH_LIMITS.center, CONFIG_ROBOT_PITCH_INVERTED, SERVO_PWM_MAX_DUTY),
        .hpoint = 0,
        .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
        .flags.output_invert = 0,
    };
    result = ledc_channel_config(&yaw_channel);
    if (result == ESP_OK) {
        result = ledc_channel_config(&pitch_channel);
    }
    if (result != ESP_OK) {
        return result;
    }

    s_state.initialized = true;
    return ESP_OK;
}

esp_err_t servo_set_yaw(int16_t angle)
{
    return set_axis(YAW_PWM_CHANNEL,
                    angle,
                    &YAW_LIMITS,
                    CONFIG_ROBOT_YAW_TRIM_DEGREES,
                    CONFIG_ROBOT_YAW_INVERTED,
                    &s_state.yaw);
}

esp_err_t servo_set_pitch(int16_t angle)
{
    return set_axis(PITCH_PWM_CHANNEL,
                    angle,
                    &PITCH_LIMITS,
                    0,
                    CONFIG_ROBOT_PITCH_INVERTED,
                    &s_state.pitch);
}

servo_control_snapshot_t servo_control_get_snapshot(void)
{
    servo_control_snapshot_t snapshot = {
        .yaw_limits = YAW_LIMITS,
        .pitch_limits = PITCH_LIMITS,
    };
    if (s_mutex != NULL && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        snapshot = s_state;
        xSemaphoreGive(s_mutex);
    }
    return snapshot;
}

esp_err_t look_center(void)
{
    esp_err_t result = servo_set_yaw(YAW_LIMITS.center);
    if (result == ESP_OK) {
        result = servo_set_pitch(PITCH_LIMITS.center);
    }
    return result;
}

esp_err_t look_left(void)
{
    return servo_set_yaw(YAW_LIMITS.minimum);
}

esp_err_t look_right(void)
{
    return servo_set_yaw(YAW_LIMITS.maximum);
}

esp_err_t look_up(void)
{
    return servo_set_pitch(PITCH_LIMITS.minimum);
}

esp_err_t look_down(void)
{
    return servo_set_pitch(PITCH_LIMITS.maximum);
}

esp_err_t nod(void)
{
    esp_err_t result = servo_set_pitch(PITCH_LIMITS.minimum);
    if (result != ESP_OK) {
        return result;
    }
    vTaskDelay(pdMS_TO_TICKS(GESTURE_DELAY_MS));
    result = servo_set_pitch(PITCH_LIMITS.maximum);
    if (result != ESP_OK) {
        return result;
    }
    vTaskDelay(pdMS_TO_TICKS(GESTURE_DELAY_MS));
    return servo_set_pitch(PITCH_LIMITS.center);
}

esp_err_t shake_head(void)
{
    esp_err_t result = servo_set_yaw(YAW_LIMITS.minimum);
    if (result != ESP_OK) {
        return result;
    }
    vTaskDelay(pdMS_TO_TICKS(GESTURE_DELAY_MS));
    result = servo_set_yaw(YAW_LIMITS.maximum);
    if (result != ESP_OK) {
        return result;
    }
    vTaskDelay(pdMS_TO_TICKS(GESTURE_DELAY_MS));
    return servo_set_yaw(YAW_LIMITS.center);
}
