#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "stream_metrics.h"

typedef struct {
    uint32_t stream_clients;
    uint64_t latest_frame_id;
    stream_metrics_snapshot_t metrics;
} stream_server_snapshot_t;

esp_err_t stream_server_start(void);
stream_server_snapshot_t stream_server_get_snapshot(void);
