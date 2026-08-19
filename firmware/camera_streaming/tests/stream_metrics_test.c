#include "stream_metrics.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

static void expect_near(double actual, double expected)
{
    assert(fabs(actual - expected) < 0.000001);
}

static void test_session_summary(void)
{
    stream_metrics_state_t state;
    stream_metrics_snapshot_t snapshot;

    stream_metrics_init(&state);
    stream_metrics_begin_session(&state, 1000000);
    stream_metrics_record_camera_frame(&state, 30000U, 5000U);
    stream_metrics_record_camera_frame(&state, 40000U, 7000U);
    stream_metrics_record_camera_failure(&state, 50000U);
    stream_metrics_record_invalid_frame(&state, 60000U);
    stream_metrics_record_header_warmup(&state);
    stream_metrics_record_header_mismatch(&state);
    stream_metrics_record_send_success(&state, 10000U, 5000U);
    stream_metrics_record_send_failure(&state, 20000U, -7);

    stream_metrics_get_snapshot(&state, 3000000, &snapshot);
    assert(snapshot.session_id == 1U);
    assert(snapshot.session_active);
    expect_near(snapshot.elapsed_seconds, 2.0);
    assert(snapshot.camera_frames == 2U);
    assert(snapshot.camera_failures == 1U);
    assert(snapshot.invalid_frames == 1U);
    assert(snapshot.header_warmup_frames == 1U);
    assert(snapshot.header_mismatch_frames == 1U);
    expect_near(snapshot.camera_fps, 1.0);
    expect_near(snapshot.average_camera_wait_ms, 45.0);
    assert(snapshot.frames_sent == 1U);
    expect_near(snapshot.stream_fps, 0.5);
    assert(snapshot.bytes_sent == 5000U);
    expect_near(snapshot.throughput_bps, 20000.0);
    expect_near(snapshot.average_jpeg_size, 6000.0);
    expect_near(snapshot.average_send_ms, 10.0);
    expect_near(snapshot.p95_send_ms, 10.0);
    assert(snapshot.send_failures == 1U);
    assert(snapshot.last_send_error_valid);
    assert(snapshot.last_send_error == -7);
}

static void test_disconnect_retains_and_reconnect_resets(void)
{
    stream_metrics_state_t state;
    stream_metrics_snapshot_t snapshot;

    stream_metrics_init(&state);
    stream_metrics_begin_session(&state, 1000000);
    stream_metrics_record_camera_frame(&state, 36000U, 6000U);
    stream_metrics_record_send_success(&state, 2000U, 6000U);
    stream_metrics_end_session(&state, 2000000);

    stream_metrics_get_snapshot(&state, 9000000, &snapshot);
    assert(!snapshot.session_active);
    expect_near(snapshot.elapsed_seconds, 1.0);
    assert(snapshot.frames_sent == 1U);

    stream_metrics_begin_session(&state, 10000000);
    stream_metrics_get_snapshot(&state, 11000000, &snapshot);
    assert(snapshot.session_id == 2U);
    assert(snapshot.session_active);
    assert(snapshot.camera_frames == 0U);
    assert(snapshot.frames_sent == 0U);
    assert(snapshot.bytes_sent == 0U);
    assert(snapshot.send_failures == 0U);
    assert(!snapshot.last_send_error_valid);
}

static void test_send_p95_histogram_boundaries(void)
{
    stream_metrics_state_t state;
    stream_metrics_snapshot_t snapshot;

    stream_metrics_init(&state);
    stream_metrics_begin_session(&state, 0);
    for (unsigned index = 0U; index < 95U; ++index) {
        stream_metrics_record_send_success(&state, 500U, 1U);
    }
    for (unsigned index = 0U; index < 5U; ++index) {
        stream_metrics_record_send_success(&state, 4500000U, 1U);
    }
    stream_metrics_get_snapshot(&state, 1000000, &snapshot);
    expect_near(snapshot.p95_send_ms, 1.0);

    stream_metrics_record_send_success(&state, 5000001U, 1U);
    for (unsigned index = 0U; index < 99U; ++index) {
        stream_metrics_record_send_success(&state, 105000U, 1U);
    }
    stream_metrics_get_snapshot(&state, 2000000, &snapshot);
    expect_near(snapshot.p95_send_ms, 110.0);
}

int main(void)
{
    test_session_summary();
    test_disconnect_retains_and_reconnect_resets();
    test_send_p95_histogram_boundaries();
    return 0;
}
