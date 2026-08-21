#include "desktop_idle_policy.h"

#include <stddef.h>

bool desktop_idle_policy_should_enter(bool enabled,
                                      bool estop_active,
                                      bool motors_stopped,
                                      uint64_t now_ms,
                                      uint64_t last_activity_ms)
{
    return enabled && !estop_active && motors_stopped &&
           now_ms >= last_activity_ms &&
           now_ms - last_activity_ms >= DESKTOP_IDLE_ENTRY_DELAY_MS;
}

uint32_t desktop_idle_policy_random_delay(uint32_t random_value,
                                          uint32_t minimum_ms,
                                          uint32_t maximum_ms)
{
    if (maximum_ms <= minimum_ms) {
        return minimum_ms;
    }
    return minimum_ms + random_value % (maximum_ms - minimum_ms + 1U);
}

int16_t desktop_idle_policy_offset_angle(const servo_axis_limits_t *limits,
                                         int16_t offset_degrees)
{
    if (limits == NULL) {
        return 0;
    }
    return servo_policy_clamp((int16_t)(limits->center + offset_degrees), limits);
}
