#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define D2_CAMERA_REFERENCE_FPS 27.683
#define STREAM_SEND_HISTOGRAM_BUCKETS 231U

typedef struct {
    uint64_t session_id;
    bool session_active;
    int64_t session_start_us;
    int64_t session_end_us;
    uint64_t camera_frames;
    uint64_t camera_failures;
    uint64_t invalid_frames;
    uint64_t header_warmup_frames;
    uint64_t header_mismatch_frames;
    uint64_t camera_wait_samples;
    uint64_t camera_wait_us_total;
    uint64_t jpeg_bytes_total;
    uint64_t frames_sent;
    uint64_t bytes_sent;
    uint64_t send_us_total;
    uint64_t send_failures;
    bool last_send_error_valid;
    int32_t last_send_error;
    uint32_t send_histogram[STREAM_SEND_HISTOGRAM_BUCKETS];
} stream_metrics_state_t;

typedef struct {
    uint64_t session_id;
    bool session_active;
    double elapsed_seconds;
    uint64_t camera_frames;
    uint64_t camera_failures;
    uint64_t invalid_frames;
    uint64_t header_warmup_frames;
    uint64_t header_mismatch_frames;
    double camera_fps;
    double average_camera_wait_ms;
    uint64_t frames_sent;
    double stream_fps;
    uint64_t bytes_sent;
    double throughput_bps;
    double average_jpeg_size;
    double average_send_ms;
    double p95_send_ms;
    uint64_t send_failures;
    bool last_send_error_valid;
    int32_t last_send_error;
} stream_metrics_snapshot_t;

void stream_metrics_init(stream_metrics_state_t *state);
void stream_metrics_begin_session(stream_metrics_state_t *state,
                                  int64_t start_us);
void stream_metrics_end_session(stream_metrics_state_t *state, int64_t end_us);
void stream_metrics_record_camera_frame(stream_metrics_state_t *state,
                                        uint32_t camera_wait_us,
                                        size_t jpeg_size);
void stream_metrics_record_camera_failure(stream_metrics_state_t *state,
                                          uint32_t camera_wait_us);
void stream_metrics_record_invalid_frame(stream_metrics_state_t *state,
                                         uint32_t camera_wait_us);
void stream_metrics_record_header_warmup(stream_metrics_state_t *state);
void stream_metrics_record_header_mismatch(stream_metrics_state_t *state);
void stream_metrics_record_send_success(stream_metrics_state_t *state,
                                        uint32_t send_us,
                                        size_t jpeg_size);
void stream_metrics_record_send_failure(stream_metrics_state_t *state,
                                        uint32_t send_us,
                                        int32_t error);
void stream_metrics_get_snapshot(const stream_metrics_state_t *state,
                                 int64_t now_us,
                                 stream_metrics_snapshot_t *snapshot);
