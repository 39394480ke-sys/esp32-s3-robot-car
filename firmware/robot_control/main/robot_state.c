#include "robot_state.h"

#include "freertos/FreeRTOS.h"

static portMUX_TYPE s_state_mux = portMUX_INITIALIZER_UNLOCKED;
static robot_state_snapshot_t s_state;

void robot_state_init(void)
{
    portENTER_CRITICAL(&s_state_mux);
    s_state = (robot_state_snapshot_t){
        .motion = ROBOT_MOTION_STOP,
        .speed_percent = 0U,
        .watchdog_armed = false,
        .estop = false,
        .last_stop_reason = ROBOT_STOP_REASON_BOOT,
    };
    portEXIT_CRITICAL(&s_state_mux);
}

void robot_state_record_motion(robot_motion_command_t motion,
                               uint8_t speed_percent,
                               bool watchdog_armed)
{
    portENTER_CRITICAL(&s_state_mux);
    s_state.motion = motion;
    s_state.speed_percent = speed_percent;
    s_state.watchdog_armed = watchdog_armed;
    portEXIT_CRITICAL(&s_state_mux);
}

void robot_state_record_stop(robot_stop_reason_t reason)
{
    portENTER_CRITICAL(&s_state_mux);
    s_state.motion = ROBOT_MOTION_STOP;
    s_state.speed_percent = 0U;
    s_state.watchdog_armed = false;
    s_state.last_stop_reason = reason;
    portEXIT_CRITICAL(&s_state_mux);
}

void robot_state_set_estop(bool active)
{
    portENTER_CRITICAL(&s_state_mux);
    s_state.estop = active;
    portEXIT_CRITICAL(&s_state_mux);
}

robot_state_snapshot_t robot_state_get_snapshot(void)
{
    robot_state_snapshot_t snapshot;
    portENTER_CRITICAL(&s_state_mux);
    snapshot = s_state;
    portEXIT_CRITICAL(&s_state_mux);
    return snapshot;
}
