#include <assert.h>
#include <math.h>
#include <stdint.h>

#include "benchmark_statistics.h"

static void expect_near(double actual, double expected)
{
    assert(fabs(actual - expected) < 0.001);
}

int main(void)
{
    benchmark_statistics_t stats;
    benchmark_statistics_reset(&stats, 1000000, 3000000);

    benchmark_statistics_record_frame(&stats, 1100000, 1000);
    benchmark_statistics_record_frame(&stats, 1200000, 2000);
    benchmark_statistics_record_failure(&stats);
    benchmark_statistics_record_frame(&stats, 1400000, 3000);
    benchmark_statistics_record_frame(&stats, 2100000, 4000);

    benchmark_summary_t summary;
    benchmark_statistics_summarize(&stats, 3000000, &summary);

    assert(summary.total_frames == 4);
    assert(summary.failed_frames == 1);
    expect_near(summary.elapsed_seconds, 2.0);
    expect_near(summary.average_fps, 2.0);
    assert(summary.minimum_window_frames == 1);
    assert(summary.maximum_window_frames == 3);
    expect_near(summary.average_jpeg_bytes, 2500.0);
    assert(summary.minimum_jpeg_bytes == 1000);
    assert(summary.maximum_jpeg_bytes == 4000);
    expect_near(summary.average_interval_ms, 333.333333);
    expect_near(summary.p50_interval_ms, 200.0);
    expect_near(summary.p95_interval_ms, 700.0);
    expect_near(summary.maximum_interval_ms, 700.0);

    return 0;
}
