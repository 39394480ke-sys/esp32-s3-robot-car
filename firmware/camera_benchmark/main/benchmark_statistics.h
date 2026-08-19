#pragma once

#include <stddef.h>
#include <stdint.h>

#define BENCHMARK_MAX_INTERVALS 20000U
#define BENCHMARK_MAX_WINDOWS 600U

typedef struct {
    int64_t start_us;
    int64_t planned_end_us;
    int64_t previous_frame_us;
    uint32_t total_frames;
    uint32_t failed_frames;
    uint64_t jpeg_bytes_total;
    size_t minimum_jpeg_bytes;
    size_t maximum_jpeg_bytes;
    uint32_t interval_count;
    uint32_t interval_us[BENCHMARK_MAX_INTERVALS];
    uint32_t window_frames[BENCHMARK_MAX_WINDOWS];
} benchmark_statistics_t;

typedef struct {
    uint32_t total_frames;
    uint32_t failed_frames;
    double elapsed_seconds;
    double average_fps;
    uint32_t minimum_window_frames;
    uint32_t maximum_window_frames;
    double average_jpeg_bytes;
    size_t minimum_jpeg_bytes;
    size_t maximum_jpeg_bytes;
    double average_interval_ms;
    double p50_interval_ms;
    double p95_interval_ms;
    double maximum_interval_ms;
} benchmark_summary_t;

void benchmark_statistics_reset(benchmark_statistics_t *stats,
                                int64_t start_us,
                                int64_t planned_end_us);
void benchmark_statistics_record_frame(benchmark_statistics_t *stats,
                                       int64_t timestamp_us,
                                       size_t jpeg_bytes);
void benchmark_statistics_record_failure(benchmark_statistics_t *stats);
void benchmark_statistics_summarize(benchmark_statistics_t *stats,
                                    int64_t end_us,
                                    benchmark_summary_t *summary);
