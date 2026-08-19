#include "stream_metrics.h"

#include <string.h>

static size_t send_histogram_bucket(uint32_t send_us)
{
    if (send_us <= 100000U) {
        return send_us == 0U ? 0U : (send_us - 1U) / 1000U;
    }
    if (send_us <= 1000000U) {
        return 100U + (send_us - 100001U) / 10000U;
    }
    if (send_us <= 5000000U) {
        return 190U + (send_us - 1000001U) / 100000U;
    }
    return STREAM_SEND_HISTOGRAM_BUCKETS - 1U;
}

static double send_bucket_upper_ms(size_t bucket)
{
    if (bucket < 100U) {
        return (double)(bucket + 1U);
    }
    if (bucket < 190U) {
        return (double)(110U + (bucket - 100U) * 10U);
    }
    if (bucket < 230U) {
        return (double)(1100U + (bucket - 190U) * 100U);
    }
    return 5000.0;
}

static double p95_send_ms(const stream_metrics_state_t *state)
{
    if (state->frames_sent == 0U) {
        return 0.0;
    }

    const uint64_t rank = (95U * state->frames_sent + 99U) / 100U;
    uint64_t cumulative = 0U;
    for (size_t bucket = 0U;
         bucket < STREAM_SEND_HISTOGRAM_BUCKETS;
         ++bucket) {
        cumulative += state->send_histogram[bucket];
        if (cumulative >= rank) {
            return send_bucket_upper_ms(bucket);
        }
    }
    return 5000.0;
}

void stream_metrics_init(stream_metrics_state_t *state)
{
    memset(state, 0, sizeof(*state));
}

void stream_metrics_begin_session(stream_metrics_state_t *state,
                                  int64_t start_us)
{
    const uint64_t next_session_id = state->session_id + 1U;
    memset(state, 0, sizeof(*state));
    state->session_id = next_session_id;
    state->session_active = true;
    state->session_start_us = start_us;
    state->session_end_us = start_us;
}

void stream_metrics_end_session(stream_metrics_state_t *state, int64_t end_us)
{
    if (!state->session_active) {
        return;
    }
    state->session_active = false;
    state->session_end_us = end_us;
}

void stream_metrics_record_camera_frame(stream_metrics_state_t *state,
                                        uint32_t camera_wait_us,
                                        size_t jpeg_size)
{
    ++state->camera_frames;
    ++state->camera_wait_samples;
    state->camera_wait_us_total += camera_wait_us;
    state->jpeg_bytes_total += jpeg_size;
}

void stream_metrics_record_camera_failure(stream_metrics_state_t *state,
                                          uint32_t camera_wait_us)
{
    ++state->camera_failures;
    ++state->camera_wait_samples;
    state->camera_wait_us_total += camera_wait_us;
}

void stream_metrics_record_invalid_frame(stream_metrics_state_t *state,
                                         uint32_t camera_wait_us)
{
    ++state->invalid_frames;
    ++state->camera_wait_samples;
    state->camera_wait_us_total += camera_wait_us;
}

void stream_metrics_record_header_warmup(stream_metrics_state_t *state)
{
    ++state->header_warmup_frames;
}

void stream_metrics_record_header_mismatch(stream_metrics_state_t *state)
{
    ++state->header_mismatch_frames;
}

void stream_metrics_record_send_success(stream_metrics_state_t *state,
                                        uint32_t send_us,
                                        size_t jpeg_size)
{
    ++state->frames_sent;
    state->bytes_sent += jpeg_size;
    state->send_us_total += send_us;
    ++state->send_histogram[send_histogram_bucket(send_us)];
}

void stream_metrics_record_send_failure(stream_metrics_state_t *state,
                                        uint32_t send_us,
                                        int32_t error)
{
    (void)send_us;
    ++state->send_failures;
    state->last_send_error_valid = true;
    state->last_send_error = error;
}

void stream_metrics_get_snapshot(const stream_metrics_state_t *state,
                                 int64_t now_us,
                                 stream_metrics_snapshot_t *snapshot)
{
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->session_id = state->session_id;
    snapshot->session_active = state->session_active;
    snapshot->camera_frames = state->camera_frames;
    snapshot->camera_failures = state->camera_failures;
    snapshot->invalid_frames = state->invalid_frames;
    snapshot->header_warmup_frames = state->header_warmup_frames;
    snapshot->header_mismatch_frames = state->header_mismatch_frames;
    snapshot->frames_sent = state->frames_sent;
    snapshot->bytes_sent = state->bytes_sent;
    snapshot->send_failures = state->send_failures;
    snapshot->last_send_error_valid = state->last_send_error_valid;
    snapshot->last_send_error = state->last_send_error;

    if (state->session_id != 0U) {
        const int64_t end_us = state->session_active
                                   ? now_us
                                   : state->session_end_us;
        if (end_us > state->session_start_us) {
            snapshot->elapsed_seconds =
                (end_us - state->session_start_us) / 1000000.0;
        }
    }

    if (snapshot->elapsed_seconds > 0.0) {
        snapshot->camera_fps =
            state->camera_frames / snapshot->elapsed_seconds;
        snapshot->stream_fps =
            state->frames_sent / snapshot->elapsed_seconds;
        snapshot->throughput_bps =
            state->bytes_sent * 8.0 / snapshot->elapsed_seconds;
    }
    if (state->camera_wait_samples > 0U) {
        snapshot->average_camera_wait_ms =
            state->camera_wait_us_total /
            (double)state->camera_wait_samples / 1000.0;
    }
    if (state->camera_frames > 0U) {
        snapshot->average_jpeg_size =
            state->jpeg_bytes_total / (double)state->camera_frames;
    }
    if (state->frames_sent > 0U) {
        snapshot->average_send_ms =
            state->send_us_total / (double)state->frames_sent / 1000.0;
        snapshot->p95_send_ms = p95_send_ms(state);
    }
}
