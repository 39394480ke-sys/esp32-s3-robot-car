#pragma once

#include <stdint.h>

void robot_control_watchdog_poll(uint64_t now_ms, void *context);
