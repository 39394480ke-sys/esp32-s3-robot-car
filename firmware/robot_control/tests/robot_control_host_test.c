#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "desktop_idle_policy.h"
#include "motor_control.h"
#include "motor_policy.h"
#include "oled_expression.h"
#include "robot_control.h"
#include "robot_control_internal.h"
#include "robot_state.h"
#include "safety.h"
#include "servo_policy.h"
#include "tts_protocol.h"

static int64_t s_now_us;
static esp_err_t s_apply_result = ESP_OK;
static unsigned s_stop_count;
static robot_motion_command_t s_last_motor_command;
static uint8_t s_last_motor_speed;

int64_t esp_timer_get_time(void)
{
    return s_now_us;
}

esp_err_t motor_control_init(void)
{
    ++s_stop_count;
    return ESP_OK;
}

esp_err_t motor_control_apply(robot_motion_command_t command,
                              uint8_t speed_percent)
{
    s_last_motor_command = command;
    s_last_motor_speed = speed_percent;
    return s_apply_result;
}

esp_err_t motor_control_stop(void)
{
    ++s_stop_count;
    return ESP_OK;
}

static void expect_stopped(robot_stop_reason_t reason)
{
    const robot_state_snapshot_t state = robot_state_get_snapshot();
    assert(state.motion == ROBOT_MOTION_STOP);
    assert(state.speed_percent == 0U);
    assert(!state.watchdog_armed);
    assert(state.last_stop_reason == reason);
}

static void expect_estop(bool active)
{
    assert(robot_estop_is_active() == active);
    assert(robot_state_get_snapshot().estop == active);
}

static void expect_motion(robot_motion_command_t motion, uint8_t speed)
{
    const robot_state_snapshot_t state = robot_state_get_snapshot();
    assert(state.motion == motion);
    assert(state.speed_percent == speed);
    assert(state.watchdog_armed);
    assert(s_last_motor_command == motion);
    assert(s_last_motor_speed == speed);
}

static void test_motor_policy(void)
{
    motor_direction_pair_t directions;
    assert(motor_policy_resolve(ROBOT_MOTION_STOP, false, false, &directions));
    assert(directions.left == MOTOR_WHEEL_STOP);
    assert(directions.right == MOTOR_WHEEL_STOP);

    assert(motor_policy_resolve(ROBOT_MOTION_FORWARD, false, false, &directions));
    assert(directions.left == MOTOR_WHEEL_FORWARD);
    assert(directions.right == MOTOR_WHEEL_FORWARD);

    assert(motor_policy_resolve(ROBOT_MOTION_BACKWARD, false, false, &directions));
    assert(directions.left == MOTOR_WHEEL_REVERSE);
    assert(directions.right == MOTOR_WHEEL_REVERSE);

    assert(motor_policy_resolve(ROBOT_MOTION_TURN_LEFT, false, false, &directions));
    assert(directions.left == MOTOR_WHEEL_REVERSE);
    assert(directions.right == MOTOR_WHEEL_FORWARD);

    assert(motor_policy_resolve(ROBOT_MOTION_TURN_RIGHT, false, false, &directions));
    assert(directions.left == MOTOR_WHEEL_FORWARD);
    assert(directions.right == MOTOR_WHEEL_REVERSE);

    assert(motor_policy_resolve(ROBOT_MOTION_FORWARD, true, false, &directions));
    assert(directions.left == MOTOR_WHEEL_REVERSE);
    assert(directions.right == MOTOR_WHEEL_FORWARD);
    assert(motor_policy_resolve(ROBOT_MOTION_FORWARD, false, true, &directions));
    assert(directions.left == MOTOR_WHEEL_FORWARD);
    assert(directions.right == MOTOR_WHEEL_REVERSE);
    assert(!motor_policy_resolve((robot_motion_command_t)99,
                                 false, false, &directions));
    assert(!motor_policy_resolve(ROBOT_MOTION_FORWARD, false, false, NULL));
}

static void test_servo_policy(void)
{
    const servo_axis_limits_t limits = {
        .minimum = 45,
        .center = 90,
        .maximum = 135,
    };
    assert(servo_policy_clamp(20, &limits) == 45);
    assert(servo_policy_clamp(90, &limits) == 90);
    assert(servo_policy_clamp(160, &limits) == 135);
    assert(servo_policy_apply_trim(90, 14) == 104);
    assert(servo_policy_apply_trim(0, -14) == 0);
    assert(servo_policy_apply_trim(180, 14) == 180);
    assert(servo_policy_angle_to_duty(
               servo_policy_apply_trim(90, 14), true, 16383U) ==
           servo_policy_angle_to_duty(104, true, 16383U));
    assert(servo_policy_angle_to_duty(0, false, 16383U) == 410U);
    assert(servo_policy_angle_to_duty(90, false, 16383U) == 1229U);
    assert(servo_policy_angle_to_duty(180, false, 16383U) == 2048U);
    assert(servo_policy_angle_to_duty(0, true, 16383U) == 2048U);
}

static void test_desktop_idle_policy(void)
{
    assert(!desktop_idle_policy_should_enter(true, false, true, 29999U, 0U));
    assert(desktop_idle_policy_should_enter(true, false, true, 30000U, 0U));
    assert(!desktop_idle_policy_should_enter(false, false, true, 30000U, 0U));
    assert(!desktop_idle_policy_should_enter(true, true, true, 30000U, 0U));
    assert(!desktop_idle_policy_should_enter(true, false, false, 30000U, 0U));
    assert(!desktop_idle_policy_should_enter(true, false, true, 10U, 20U));

    assert(desktop_idle_policy_random_delay(0U, 15000U, 45000U) == 15000U);
    assert(desktop_idle_policy_random_delay(30000U, 15000U, 45000U) ==
           45000U);
    assert(desktop_idle_policy_random_delay(123U, 1000U, 1000U) == 1000U);

    const servo_axis_limits_t yaw = {
        .minimum = 45,
        .center = 90,
        .maximum = 135,
    };
    const servo_axis_limits_t narrow = {
        .minimum = 85,
        .center = 90,
        .maximum = 95,
    };
    assert(desktop_idle_policy_offset_angle(&yaw, -10) == 80);
    assert(desktop_idle_policy_offset_angle(&yaw, 10) == 100);
    assert(desktop_idle_policy_offset_angle(&narrow, -10) == 85);
    assert(desktop_idle_policy_offset_angle(&narrow, 10) == 95);
    assert(desktop_idle_policy_offset_angle(NULL, 10) == 0);
}

static void test_oled_expressions(void)
{
    static const char *const names[] = {
        "idle", "happy", "curious", "confused",
        "sleepy", "watching", "warning", "excited",
    };
    for (int index = 0; index < OLED_EXPRESSION_COUNT; ++index) {
        oled_expression_id_t expression = OLED_EXPRESSION_IDLE;
        assert(oled_expression_from_name(names[index], &expression));
        assert(expression == (oled_expression_id_t)index);
        assert(strcmp(oled_expression_name(expression), names[index]) == 0);
    }
    oled_expression_id_t expression;
    assert(!oled_expression_from_name("unknown", &expression));
    assert(!oled_expression_from_name(NULL, &expression));
    assert(strcmp(oled_expression_name((oled_expression_id_t)99), "unknown") == 0);
}

static void test_tts_protocol(void)
{
    static const char *const names[] = {
        "hello", "here", "received", "stopped",
    };
    for (int index = 0; index < TTS_PHRASE_COUNT; ++index) {
        tts_phrase_id_t phrase = TTS_PHRASE_HELLO;
        assert(tts_phrase_from_name(names[index], &phrase));
        assert(phrase == (tts_phrase_id_t)index);
        assert(strcmp(tts_phrase_name(phrase), names[index]) == 0);
        assert(tts_phrase_text(phrase) != NULL);
    }
    tts_phrase_id_t phrase;
    assert(!tts_phrase_from_name("unknown", &phrase));
    assert(strcmp(tts_phrase_name((tts_phrase_id_t)99), "unknown") == 0);

    uint8_t frame[16];
    assert(tts_protocol_build_frame("hi", frame, sizeof(frame)) == 7U);
    assert(frame[0] == 0xFD);
    assert(frame[1] == 0x00);
    assert(frame[2] == 0x04);
    assert(frame[3] == 0x01);
    assert(frame[4] == 0x04);
    assert(frame[5] == 'h' && frame[6] == 'i');
    assert(tts_protocol_build_frame("", frame, sizeof(frame)) == 0U);
}

static void test_robot_control(void)
{
    assert(robot_control_init() == ESP_OK);
    expect_stopped(ROBOT_STOP_REASON_BOOT);
    expect_estop(false);

    s_now_us = 1000000;
    assert(robot_move_forward(40U) == ESP_OK);
    expect_motion(ROBOT_MOTION_FORWARD, 40U);
    assert(robot_move_backward(100U) == ESP_OK);
    expect_motion(ROBOT_MOTION_BACKWARD, 100U);
    assert(robot_turn_left(35U) == ESP_OK);
    expect_motion(ROBOT_MOTION_TURN_LEFT, 35U);
    assert(robot_turn_right(35U) == ESP_OK);
    expect_motion(ROBOT_MOTION_TURN_RIGHT, 35U);

    assert(robot_drive(ROBOT_MOTION_FORWARD, 0U) == ESP_OK);
    expect_stopped(ROBOT_STOP_REASON_ZERO_SPEED);

    assert(robot_drive((robot_motion_command_t)99, 40U) == ESP_ERR_INVALID_ARG);
    expect_stopped(ROBOT_STOP_REASON_INVALID_COMMAND);
    assert(robot_drive(ROBOT_MOTION_FORWARD, 101U) == ESP_ERR_INVALID_ARG);
    expect_stopped(ROBOT_STOP_REASON_INVALID_COMMAND);

    assert(robot_drive(ROBOT_MOTION_STOP, 40U) == ESP_OK);
    expect_stopped(ROBOT_STOP_REASON_EXPLICIT);
    robot_notify_wifi_disconnected();
    expect_stopped(ROBOT_STOP_REASON_WIFI_DISCONNECTED);

    s_now_us = 1500000;
    assert(robot_move_forward(40U) == ESP_OK);
    robot_estop_activate();
    expect_stopped(ROBOT_STOP_REASON_ESTOP);
    expect_estop(true);
    assert(robot_move_forward(40U) == ESP_ERR_INVALID_STATE);
    assert(robot_move_backward(40U) == ESP_ERR_INVALID_STATE);
    assert(robot_turn_left(40U) == ESP_ERR_INVALID_STATE);
    assert(robot_turn_right(40U) == ESP_ERR_INVALID_STATE);
    expect_stopped(ROBOT_STOP_REASON_ESTOP);
    expect_estop(true);
    robot_stop();
    expect_stopped(ROBOT_STOP_REASON_EXPLICIT);
    expect_estop(true);
    robot_notify_wifi_disconnected();
    expect_stopped(ROBOT_STOP_REASON_WIFI_DISCONNECTED);
    expect_estop(true);
    robot_estop_clear();
    expect_stopped(ROBOT_STOP_REASON_ESTOP);
    expect_estop(false);
    assert(robot_move_forward(20U) == ESP_OK);
    expect_motion(ROBOT_MOTION_FORWARD, 20U);
    robot_stop();

    s_now_us = 2000000;
    assert(robot_move_forward(40U) == ESP_OK);
    robot_control_watchdog_poll(2799U, NULL);
    expect_motion(ROBOT_MOTION_FORWARD, 40U);
    robot_control_watchdog_poll(2800U, NULL);
    expect_stopped(ROBOT_STOP_REASON_WATCHDOG_TIMEOUT);
    const unsigned stops_after_timeout = s_stop_count;
    robot_control_watchdog_poll(3000U, NULL);
    assert(s_stop_count == stops_after_timeout);
    expect_stopped(ROBOT_STOP_REASON_WATCHDOG_TIMEOUT);

    s_now_us = 3000000;
    assert(robot_move_forward(40U) == ESP_OK);
    s_now_us = 3799000;
    assert(robot_move_forward(40U) == ESP_OK);
    robot_control_watchdog_poll(4598U, NULL);
    expect_motion(ROBOT_MOTION_FORWARD, 40U);
    robot_control_watchdog_poll(4599U, NULL);
    expect_stopped(ROBOT_STOP_REASON_WATCHDOG_TIMEOUT);

    const unsigned stops_before_failure = s_stop_count;
    s_apply_result = ESP_FAIL;
    assert(robot_move_forward(40U) == ESP_FAIL);
    assert(s_stop_count == stops_before_failure + 1U);
    expect_stopped(ROBOT_STOP_REASON_DRIVER_ERROR);
}

int main(void)
{
    test_motor_policy();
    test_servo_policy();
    test_desktop_idle_policy();
    test_oled_expressions();
    test_tts_protocol();
    test_robot_control();
    puts("robot_control host tests passed");
    return 0;
}
