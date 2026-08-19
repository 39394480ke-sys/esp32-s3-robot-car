#include "benchmark_statistics.h"

#include <stdlib.h>
#include <string.h>

static int compare_u32(const void *left, const void *right)
{
    const uint32_t lhs = *(const uint32_t *)left;
    const uint32_t rhs = *(const uint32_t *)right;
    return (lhs > rhs) - (lhs < rhs);
}

static double percentile_ms(const benchmark_statistics_t *stats, unsigned percent)
{
    if (stats->interval_count == 0U) {
        return 0.0;
    }

    const size_t rank =
        ((size_t)percent * stats->interval_count + 99U) / 100U;
    return stats->interval_us[rank - 1U] / 1000.0;
}

void benchmark_statistics_reset(benchmark_statistics_t *stats,
                                int64_t start_us,
                                int64_t planned_end_us)
{
    memset(stats, 0, sizeof(*stats));
    stats->start_us = start_us;
    stats->planned_end_us = planned_end_us;
    stats->minimum_jpeg_bytes = SIZE_MAX;
}

void benchmark_statistics_record_frame(benchmark_statistics_t *stats,
                                       int64_t timestamp_us,
                                       size_t jpeg_bytes)
{
    ++stats->total_frames;
    stats->jpeg_bytes_total += jpeg_bytes;
    if (jpeg_bytes < stats->minimum_jpeg_bytes) {
        stats->minimum_jpeg_bytes = jpeg_bytes;
    }
    if (jpeg_bytes > stats->maximum_jpeg_bytes) {
        stats->maximum_jpeg_bytes = jpeg_bytes;
    }

    if (stats->previous_frame_us != 0 &&
        stats->interval_count < BENCHMARK_MAX_INTERVALS) {
        const int64_t interval = timestamp_us - stats->previous_frame_us;
        stats->interval_us[stats->interval_count++] =
            interval > 0 ? (uint32_t)interval : 0U;
    }
    stats->previous_frame_us = timestamp_us;

    const int64_t since_start = timestamp_us - stats->start_us;
    if (since_start >= 0) {
        const size_t window = (size_t)(since_start / 1000000);
        if (window < BENCHMARK_MAX_WINDOWS) {
            ++stats->window_frames[window];
        }
    }
}

void benchmark_statistics_record_failure(benchmark_statistics_t *stats)
{
    ++stats->failed_frames;
}

void benchmark_statistics_summarize(benchmark_statistics_t *stats,
                                    int64_t end_us,
                                    benchmark_summary_t *summary)
{
    memset(summary, 0, sizeof(*summary));
    summary->total_frames = stats->total_frames;
    summary->failed_frames = stats->failed_frames;
    summary->elapsed_seconds = (end_us - stats->start_us) / 1000000.0;
    if (summary->elapsed_seconds > 0.0) {
        summary->average_fps = stats->total_frames / summary->elapsed_seconds;
    }

    const int64_t planned_duration_us = stats->planned_end_us - stats->start_us;
    size_t window_count = (size_t)(planned_duration_us / 1000000);
    if (window_count > BENCHMARK_MAX_WINDOWS) {
        window_count = BENCHMARK_MAX_WINDOWS;
    }
    summary->minimum_window_frames = UINT32_MAX;
    for (size_t index = 0U; index < window_count; ++index) {
        const uint32_t count = stats->window_frames[index];
        if (count < summary->minimum_window_frames) {
            summary->minimum_window_frames = count;
        }
        if (count > summary->maximum_window_frames) {
            summary->maximum_window_frames = count;
        }
    }
    if (window_count == 0U) {
        summary->minimum_window_frames = 0U;
    }

    if (stats->total_frames > 0U) {
        summary->average_jpeg_bytes =
            (double)stats->jpeg_bytes_total / stats->total_frames;
        summary->minimum_jpeg_bytes = stats->minimum_jpeg_bytes;
        summary->maximum_jpeg_bytes = stats->maximum_jpeg_bytes;
    }

    if (stats->interval_count > 0U) {
        uint64_t interval_total_us = 0U;
        for (size_t index = 0U; index < stats->interval_count; ++index) {
            interval_total_us += stats->interval_us[index];
        }
        summary->average_interval_ms =
            (double)interval_total_us / stats->interval_count / 1000.0;
        qsort(stats->interval_us,
              stats->interval_count,
              sizeof(stats->interval_us[0]),
              compare_u32);
        summary->p50_interval_ms = percentile_ms(stats, 50U);
        summary->p95_interval_ms = percentile_ms(stats, 95U);
        summary->maximum_interval_ms =
            stats->interval_us[stats->interval_count - 1U] / 1000.0;
    }
}
