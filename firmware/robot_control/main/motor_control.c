#include "motor_control.h"

#include <stdbool.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "sdkconfig.h"

#include "motor_policy.h"

#ifndef CONFIG_ROBOT_LEFT_MOTOR_INVERTED
#define CONFIG_ROBOT_LEFT_MOTOR_INVERTED 0
#endif

#ifndef CONFIG_ROBOT_RIGHT_MOTOR_INVERTED
#define CONFIG_ROBOT_RIGHT_MOTOR_INVERTED 0
#endif

#define LEFT_PWM_GPIO GPIO_NUM_40
#define LEFT_IN1_GPIO GPIO_NUM_41
#define LEFT_IN2_GPIO GPIO_NUM_42
#define RIGHT_PWM_GPIO GPIO_NUM_45
#define RIGHT_IN1_GPIO GPIO_NUM_46
#define RIGHT_IN2_GPIO GPIO_NUM_48

#define MOTOR_PWM_FREQUENCY_HZ 20000
#define MOTOR_PWM_MAX_DUTY ((1U << 10U) - 1U)

static const ledc_mode_t MOTOR_PWM_MODE = LEDC_LOW_SPEED_MODE;
static const ledc_timer_t MOTOR_PWM_TIMER = LEDC_TIMER_0;
static const ledc_channel_t LEFT_PWM_CHANNEL = LEDC_CHANNEL_0;
static const ledc_channel_t RIGHT_PWM_CHANNEL = LEDC_CHANNEL_1;

static bool s_initialized;

static esp_err_t preserve_first_error(esp_err_t first, esp_err_t current)
{
    return first == ESP_OK ? current : first;
}

static esp_err_t configure_safe_gpio_levels(void)
{
    const uint64_t output_mask =
        (1ULL << LEFT_PWM_GPIO) |
        (1ULL << LEFT_IN1_GPIO) |
        (1ULL << LEFT_IN2_GPIO) |
        (1ULL << RIGHT_PWM_GPIO) |
        (1ULL << RIGHT_IN1_GPIO) |
        (1ULL << RIGHT_IN2_GPIO);
    const gpio_config_t config = {
        .pin_bit_mask = output_mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t result = gpio_config(&config);
    result = preserve_first_error(result, gpio_set_level(LEFT_PWM_GPIO, 0));
    result = preserve_first_error(result, gpio_set_level(RIGHT_PWM_GPIO, 0));
    result = preserve_first_error(result, gpio_set_level(LEFT_IN1_GPIO, 0));
    result = preserve_first_error(result, gpio_set_level(LEFT_IN2_GPIO, 0));
    result = preserve_first_error(result, gpio_set_level(RIGHT_IN1_GPIO, 0));
    result = preserve_first_error(result, gpio_set_level(RIGHT_IN2_GPIO, 0));
    return result;
}

static esp_err_t set_wheel_direction(gpio_num_t in1,
                                     gpio_num_t in2,
                                     motor_wheel_direction_t direction)
{
    int first_level = 0;
    int second_level = 0;
    if (direction == MOTOR_WHEEL_FORWARD) {
        first_level = 1;
    } else if (direction == MOTOR_WHEEL_REVERSE) {
        second_level = 1;
    }

    esp_err_t result = gpio_set_level(in1, first_level);
    result = preserve_first_error(result, gpio_set_level(in2, second_level));
    return result;
}

static esp_err_t set_pwm_duty(ledc_channel_t channel, uint32_t duty)
{
    esp_err_t result = ledc_set_duty(MOTOR_PWM_MODE, channel, duty);
    if (result == ESP_OK) {
        result = ledc_update_duty(MOTOR_PWM_MODE, channel);
    }
    return result;
}

esp_err_t motor_control_stop(void)
{
    esp_err_t result = ESP_OK;
    if (s_initialized) {
        result = set_pwm_duty(LEFT_PWM_CHANNEL, 0U);
        result = preserve_first_error(
            result, set_pwm_duty(RIGHT_PWM_CHANNEL, 0U));
    } else {
        result = gpio_set_level(LEFT_PWM_GPIO, 0);
        result = preserve_first_error(
            result, gpio_set_level(RIGHT_PWM_GPIO, 0));
    }

    result = preserve_first_error(
        result, set_wheel_direction(LEFT_IN1_GPIO, LEFT_IN2_GPIO,
                                    MOTOR_WHEEL_STOP));
    result = preserve_first_error(
        result, set_wheel_direction(RIGHT_IN1_GPIO, RIGHT_IN2_GPIO,
                                    MOTOR_WHEEL_STOP));
    return result;
}

esp_err_t motor_control_init(void)
{
    s_initialized = false;
    esp_err_t result = configure_safe_gpio_levels();
    if (result != ESP_OK) {
        return result;
    }

    const ledc_timer_config_t timer_config = {
        .speed_mode = MOTOR_PWM_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = MOTOR_PWM_TIMER,
        .freq_hz = MOTOR_PWM_FREQUENCY_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
        .deconfigure = false,
    };
    result = ledc_timer_config(&timer_config);
    if (result != ESP_OK) {
        return result;
    }

    const ledc_channel_config_t left_channel = {
        .gpio_num = LEFT_PWM_GPIO,
        .speed_mode = MOTOR_PWM_MODE,
        .channel = LEFT_PWM_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = MOTOR_PWM_TIMER,
        .duty = 0U,
        .hpoint = 0,
        .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
        .flags.output_invert = 0,
    };
    const ledc_channel_config_t right_channel = {
        .gpio_num = RIGHT_PWM_GPIO,
        .speed_mode = MOTOR_PWM_MODE,
        .channel = RIGHT_PWM_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = MOTOR_PWM_TIMER,
        .duty = 0U,
        .hpoint = 0,
        .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
        .flags.output_invert = 0,
    };
    result = ledc_channel_config(&left_channel);
    if (result == ESP_OK) {
        result = ledc_channel_config(&right_channel);
    }
    if (result != ESP_OK) {
        ledc_stop(MOTOR_PWM_MODE, LEFT_PWM_CHANNEL, 0U);
        ledc_stop(MOTOR_PWM_MODE, RIGHT_PWM_CHANNEL, 0U);
        configure_safe_gpio_levels();
        return result;
    }

    s_initialized = true;
    return motor_control_stop();
}

esp_err_t motor_control_apply(robot_motion_command_t command,
                              uint8_t speed_percent)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (command == ROBOT_MOTION_STOP || speed_percent == 0U) {
        return motor_control_stop();
    }
    if (speed_percent > 100U) {
        motor_control_stop();
        return ESP_ERR_INVALID_ARG;
    }

    motor_direction_pair_t directions;
    if (!motor_policy_resolve(command,
                              CONFIG_ROBOT_LEFT_MOTOR_INVERTED,
                              CONFIG_ROBOT_RIGHT_MOTOR_INVERTED,
                              &directions)) {
        motor_control_stop();
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result = set_pwm_duty(LEFT_PWM_CHANNEL, 0U);
    result = preserve_first_error(
        result, set_pwm_duty(RIGHT_PWM_CHANNEL, 0U));
    result = preserve_first_error(
        result, set_wheel_direction(LEFT_IN1_GPIO, LEFT_IN2_GPIO,
                                    directions.left));
    result = preserve_first_error(
        result, set_wheel_direction(RIGHT_IN1_GPIO, RIGHT_IN2_GPIO,
                                    directions.right));
    if (result != ESP_OK) {
        motor_control_stop();
        return result;
    }

    const uint32_t duty =
        ((uint32_t)speed_percent * MOTOR_PWM_MAX_DUTY + 50U) / 100U;
    result = set_pwm_duty(LEFT_PWM_CHANNEL, duty);
    result = preserve_first_error(
        result, set_pwm_duty(RIGHT_PWM_CHANNEL, duty));
    if (result != ESP_OK) {
        motor_control_stop();
    }
    return result;
}
