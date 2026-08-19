#include "camera_benchmark.h"

#include <inttypes.h>

#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "benchmark_statistics.h"
#include "camera_init.h"

#define BENCHMARK_WARMUP_FRAMES 30U
#define BENCHMARK_DURATION_SECONDS 60U
#define QUALITY_DIAGNOSTIC_DURATION_SECONDS 15U
#define FB_COUNT_DIAGNOSTIC_DURATION_SECONDS 15U
#define PSRAM_DMA_DIAGNOSTIC_DURATION_SECONDS 15U
#define QQVGA_DIAGNOSTIC_DURATION_SECONDS 15U
#define OV3660_TIMING_PCLK_DIV 9U
#define MAX_WARMUP_FAILURES 30U
#define QVGA_WIDTH 320U
#define QVGA_HEIGHT 240U
#define QQVGA_WIDTH 160U
#define QQVGA_HEIGHT 120U

static const char *TAG = "camera_benchmark";

typedef struct {
    size_t internal_free;
    size_t internal_minimum_free;
    size_t internal_largest;
    size_t psram_free;
    size_t psram_minimum_free;
    size_t psram_largest;
} heap_snapshot_t;

static heap_snapshot_t take_heap_snapshot(void)
{
    const uint32_t internal_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const uint32_t psram_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    return (heap_snapshot_t) {
        .internal_free = heap_caps_get_free_size(internal_caps),
        .internal_minimum_free = heap_caps_get_minimum_free_size(internal_caps),
        .internal_largest = heap_caps_get_largest_free_block(internal_caps),
        .psram_free = heap_caps_get_free_size(psram_caps),
        .psram_minimum_free = heap_caps_get_minimum_free_size(psram_caps),
        .psram_largest = heap_caps_get_largest_free_block(psram_caps),
    };
}

static void log_heap_snapshot(const char *phase, const heap_snapshot_t *snapshot)
{
    ESP_LOGI(TAG,
             "heap %s: internal free=%u min=%u largest=%u; "
             "PSRAM free=%u min=%u largest=%u",
             phase,
             (unsigned)snapshot->internal_free,
             (unsigned)snapshot->internal_minimum_free,
             (unsigned)snapshot->internal_largest,
             (unsigned)snapshot->psram_free,
             (unsigned)snapshot->psram_minimum_free,
             (unsigned)snapshot->psram_largest);
}

static bool frame_is_expected_jpeg(const camera_fb_t *frame,
                                   uint16_t expected_width,
                                   uint16_t expected_height)
{
    return frame != NULL &&
           frame->format == PIXFORMAT_JPEG &&
           frame->width == expected_width &&
           frame->height == expected_height &&
           frame->len >= 2U &&
           frame->buf[0] == 0xffU &&
           frame->buf[1] == 0xd8U &&
           frame->buf[frame->len - 2U] == 0xffU &&
           frame->buf[frame->len - 1U] == 0xd9U;
}

static bool run_warmup(uint16_t expected_width, uint16_t expected_height)
{
    unsigned successful = 0U;
    unsigned failed = 0U;
    while (successful < BENCHMARK_WARMUP_FRAMES &&
           failed <= MAX_WARMUP_FAILURES) {
        camera_fb_t *frame = esp_camera_fb_get();
        if (frame_is_expected_jpeg(frame, expected_width, expected_height)) {
            if (successful == 0U) {
                ESP_LOGI(TAG,
                         "framebuffer verification: width=%u height=%u "
                         "format=%d JPEG=YES",
                         frame->width,
                         frame->height,
                         frame->format);
            }
            ++successful;
        } else {
            ++failed;
        }
        if (frame != NULL) {
            esp_camera_fb_return(frame);
        }
    }

    ESP_LOGI(TAG,
             "warm-up complete: discarded=%u failures=%u",
             successful,
             failed);
    return successful == BENCHMARK_WARMUP_FRAMES;
}

const char *camera_benchmark_result_name(camera_benchmark_result_t result)
{
    switch (result) {
    case CAMERA_BENCHMARK_PASS:
        return "PASS";
    case CAMERA_BENCHMARK_PARTIAL:
        return "PARTIAL";
    default:
        return "FAIL";
    }
}

static bool run_measurement_with_pclk_div(uint8_t jpeg_quality,
                                          uint8_t fb_count,
                                          bool psram_dma,
                                          framesize_t frame_size,
                                          uint16_t expected_width,
                                          uint16_t expected_height,
                                          unsigned duration_seconds,
                                          const char *label,
                                          benchmark_summary_t *summary,
                                          uint8_t ov3660_pclk_div)
{
    benchmark_statistics_t *stats = heap_caps_calloc(
        1U, sizeof(*stats), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (stats == NULL) {
        ESP_LOGE(TAG, "unable to allocate benchmark statistics in PSRAM");
        return false;
    }

    if (camera_init_with_frame_settings(jpeg_quality,
                                        fb_count,
                                        psram_dma,
                                        frame_size) != ESP_OK) {
        heap_caps_free(stats);
        return false;
    }
    if (ov3660_pclk_div != 0U &&
        camera_apply_ov3660_pclk_div(ov3660_pclk_div) != ESP_OK) {
        camera_shutdown();
        heap_caps_free(stats);
        return false;
    }
    if (!run_warmup(expected_width, expected_height)) {
        ESP_LOGE(TAG, "benchmark aborted because warm-up did not complete");
        camera_shutdown();
        heap_caps_free(stats);
        return false;
    }

    const heap_snapshot_t heap_before = take_heap_snapshot();
    log_heap_snapshot("before", &heap_before);

    const int64_t start_us = esp_timer_get_time();
    const int64_t planned_end_us =
        start_us + (int64_t)duration_seconds * 1000000;
    benchmark_statistics_reset(stats, start_us, planned_end_us);
    unsigned last_logged_second = 0U;

    ESP_LOGI(TAG,
             "%s start: resolution=%ux%u quality=%u fb_count=%u "
             "PSRAM_DMA=%s duration=%us "
             "real esp_camera_fb_get frames only",
             label,
             expected_width,
             expected_height,
             jpeg_quality,
             fb_count,
             psram_dma ? "ON" : "OFF",
             duration_seconds);
    while (esp_timer_get_time() < planned_end_us) {
        camera_fb_t *frame = esp_camera_fb_get();
        const int64_t timestamp_us = esp_timer_get_time();

        if (timestamp_us < planned_end_us) {
            if (frame_is_expected_jpeg(frame,
                                       expected_width,
                                       expected_height)) {
                benchmark_statistics_record_frame(stats,
                                                  timestamp_us,
                                                  frame->len);
            } else {
                benchmark_statistics_record_failure(stats);
            }
        }
        if (frame != NULL) {
            esp_camera_fb_return(frame);
        }

        unsigned elapsed_second =
            (unsigned)((timestamp_us - start_us) / 1000000);
        if (elapsed_second > duration_seconds) {
            elapsed_second = duration_seconds;
        }
        if (elapsed_second > last_logged_second) {
            const unsigned completed_window = elapsed_second - 1U;
            const double average_fps =
                (double)stats->total_frames /
                ((timestamp_us - start_us) / 1000000.0);
            const double average_jpeg = stats->total_frames > 0U
                ? (double)stats->jpeg_bytes_total / stats->total_frames
                : 0.0;
            const heap_snapshot_t current_heap = take_heap_snapshot();
            ESP_LOGI(TAG,
                     "progress: time=%us window_frames=%u avg_fps=%.2f "
                     "avg_jpeg=%.0fB failures=%u internal_free=%u "
                     "psram_free=%u",
                     elapsed_second,
                     stats->window_frames[completed_window],
                     average_fps,
                     average_jpeg,
                     stats->failed_frames,
                     (unsigned)current_heap.internal_free,
                     (unsigned)current_heap.psram_free);
            last_logged_second = elapsed_second;
        }
    }

    const int64_t end_us = planned_end_us;
    const heap_snapshot_t heap_after = take_heap_snapshot();
    benchmark_statistics_summarize(stats, end_us, summary);

    ESP_LOGI(TAG,
             "%s summary: quality=%u fb_count=%u total_frames=%u "
             "failed_frames=%u elapsed=%.3fs "
             "average_fps=%.3f min_1s=%u max_1s=%u",
             label,
             jpeg_quality,
             fb_count,
             summary->total_frames,
             summary->failed_frames,
             summary->elapsed_seconds,
             summary->average_fps,
             summary->minimum_window_frames,
             summary->maximum_window_frames);
    ESP_LOGI(TAG,
             "%s JPEG: average=%.1fB minimum=%uB maximum=%uB",
             label,
             summary->average_jpeg_bytes,
             (unsigned)summary->minimum_jpeg_bytes,
             (unsigned)summary->maximum_jpeg_bytes);
    ESP_LOGI(TAG,
             "%s interval: average=%.3fms P50=%.3fms P95=%.3fms "
             "maximum=%.3fms",
             label,
             summary->average_interval_ms,
             summary->p50_interval_ms,
             summary->p95_interval_ms,
             summary->maximum_interval_ms);
    log_heap_snapshot("after", &heap_after);
    ESP_LOGI(TAG,
             "heap delta: internal_free=%d PSRAM_free=%d",
             (int)heap_after.internal_free - (int)heap_before.internal_free,
             (int)heap_after.psram_free - (int)heap_before.psram_free);

    const bool shutdown_ok = camera_shutdown() == ESP_OK;
    const bool completed = shutdown_ok && summary->total_frames > 0U;
    heap_caps_free(stats);
    return completed;
}

static bool run_measurement(uint8_t jpeg_quality,
                            uint8_t fb_count,
                            bool psram_dma,
                            framesize_t frame_size,
                            uint16_t expected_width,
                            uint16_t expected_height,
                            unsigned duration_seconds,
                            const char *label,
                            benchmark_summary_t *summary)
{
    return run_measurement_with_pclk_div(jpeg_quality,
                                         fb_count,
                                         psram_dma,
                                         frame_size,
                                         expected_width,
                                         expected_height,
                                         duration_seconds,
                                         label,
                                         summary,
                                         0U);
}

camera_benchmark_result_t camera_run_baseline_benchmark(void)
{
    benchmark_summary_t summary = {0};
    const bool completed = run_measurement(20U,
                                           2U,
                                           false,
                                           FRAMESIZE_QVGA,
                                           QVGA_WIDTH,
                                           QVGA_HEIGHT,
                                           BENCHMARK_DURATION_SECONDS,
                                           "baseline",
                                           &summary);
    camera_benchmark_result_t result = CAMERA_BENCHMARK_FAIL;
    if (completed && summary.failed_frames == 0U) {
        result = summary.average_fps >= 30.0 &&
                         summary.minimum_window_frames >= 29U &&
                         summary.p95_interval_ms <= 40.0
                     ? CAMERA_BENCHMARK_PASS
                     : CAMERA_BENCHMARK_PARTIAL;
    }
    ESP_LOGI(TAG,
             "D1_BENCHMARK_RESULT=%s",
             camera_benchmark_result_name(result));
    return result;
}

bool camera_run_quality_diagnostic(void)
{
    static const uint8_t qualities[] = {12U, 20U, 30U, 40U};
    bool all_completed = true;

    for (size_t index = 0U; index < sizeof(qualities) / sizeof(qualities[0]); ++index) {
        benchmark_summary_t summary = {0};
        const bool completed = run_measurement(qualities[index],
                                               2U,
                                               false,
                                               FRAMESIZE_QVGA,
                                               QVGA_WIDTH,
                                               QVGA_HEIGHT,
                                               QUALITY_DIAGNOSTIC_DURATION_SECONDS,
                                               "quality diagnostic",
                                               &summary);
        ESP_LOGI(TAG,
                 "QUALITY_DIAGNOSTIC_RESULT quality=%u completed=%s "
                 "fps=%.3f min_1s=%u P95=%.3fms avg_jpeg=%.1fB failures=%u",
                 qualities[index],
                 completed ? "YES" : "NO",
                 summary.average_fps,
                 summary.minimum_window_frames,
                 summary.p95_interval_ms,
                 summary.average_jpeg_bytes,
                 summary.failed_frames);
        all_completed = all_completed && completed;
    }

    return all_completed;
}

bool camera_run_fb_count_diagnostic(void)
{
    static const uint8_t fb_counts[] = {1U, 2U};
    bool all_completed = true;

    for (size_t index = 0U; index < sizeof(fb_counts) / sizeof(fb_counts[0]); ++index) {
        benchmark_summary_t summary = {0};
        const bool completed = run_measurement(20U,
                                               fb_counts[index],
                                               false,
                                               FRAMESIZE_QVGA,
                                               QVGA_WIDTH,
                                               QVGA_HEIGHT,
                                               FB_COUNT_DIAGNOSTIC_DURATION_SECONDS,
                                               "fb_count diagnostic",
                                               &summary);
        ESP_LOGI(TAG,
                 "FB_COUNT_DIAGNOSTIC_RESULT fb_count=%u completed=%s "
                 "fps=%.3f min_1s=%u P95=%.3fms avg_jpeg=%.1fB failures=%u",
                 fb_counts[index],
                 completed ? "YES" : "NO",
                 summary.average_fps,
                 summary.minimum_window_frames,
                 summary.p95_interval_ms,
                 summary.average_jpeg_bytes,
                 summary.failed_frames);
        all_completed = all_completed && completed;
    }

    return all_completed;
}

bool camera_run_psram_dma_diagnostic(void)
{
    static const bool dma_modes[] = {false, true};
    bool all_completed = true;

    for (size_t index = 0U; index < sizeof(dma_modes) / sizeof(dma_modes[0]); ++index) {
        benchmark_summary_t summary = {0};
        const bool completed = run_measurement(20U,
                                               2U,
                                               dma_modes[index],
                                               FRAMESIZE_QVGA,
                                               QVGA_WIDTH,
                                               QVGA_HEIGHT,
                                               PSRAM_DMA_DIAGNOSTIC_DURATION_SECONDS,
                                               "PSRAM DMA diagnostic",
                                               &summary);
        ESP_LOGI(TAG,
                 "PSRAM_DMA_DIAGNOSTIC_RESULT mode=%s completed=%s "
                 "fps=%.3f min_1s=%u P95=%.3fms avg_jpeg=%.1fB failures=%u",
                 dma_modes[index] ? "ON" : "OFF",
                 completed ? "YES" : "NO",
                 summary.average_fps,
                 summary.minimum_window_frames,
                 summary.p95_interval_ms,
                 summary.average_jpeg_bytes,
                 summary.failed_frames);
        all_completed = all_completed && completed;
    }

    return all_completed;
}

static camera_benchmark_result_t run_qqvga_benchmark(unsigned duration_seconds,
                                                      const char *label)
{
    benchmark_summary_t summary = {0};
    const bool completed = run_measurement(20U,
                                           2U,
                                           false,
                                           FRAMESIZE_QQVGA,
                                           QQVGA_WIDTH,
                                           QQVGA_HEIGHT,
                                           duration_seconds,
                                           label,
                                           &summary);

    camera_benchmark_result_t result = CAMERA_BENCHMARK_FAIL;
    if (completed && summary.failed_frames == 0U) {
        result = summary.average_fps >= 30.0
                     ? CAMERA_BENCHMARK_PASS
                     : CAMERA_BENCHMARK_PARTIAL;
    }
    ESP_LOGI(TAG,
             "%s duration=%us result=%s fps=%.3f min_1s=%u max_1s=%u "
             "P95=%.3fms failures=%u",
             duration_seconds == QQVGA_DIAGNOSTIC_DURATION_SECONDS
                 ? "QQVGA_SHORT_RESULT"
                 : "QQVGA_FORMAL_RESULT",
             duration_seconds,
             camera_benchmark_result_name(result),
             summary.average_fps,
             summary.minimum_window_frames,
             summary.maximum_window_frames,
             summary.p95_interval_ms,
             summary.failed_frames);
    return result;
}

camera_benchmark_result_t camera_run_qqvga_short_diagnostic(void)
{
    return run_qqvga_benchmark(QQVGA_DIAGNOSTIC_DURATION_SECONDS,
                               "QQVGA short diagnostic");
}

camera_benchmark_result_t camera_run_qqvga_formal_benchmark(void)
{
    return run_qqvga_benchmark(BENCHMARK_DURATION_SECONDS,
                               "QQVGA formal benchmark");
}

camera_benchmark_result_t camera_run_qqvga_pclk_diagnostic(void)
{
    benchmark_summary_t summary = {0};
    const bool completed = run_measurement_with_pclk_div(
        20U,
        2U,
        false,
        FRAMESIZE_QQVGA,
        QQVGA_WIDTH,
        QQVGA_HEIGHT,
        QQVGA_DIAGNOSTIC_DURATION_SECONDS,
        "QQVGA PCLK diagnostic",
        &summary,
        OV3660_TIMING_PCLK_DIV);

    camera_benchmark_result_t result = CAMERA_BENCHMARK_FAIL;
    if (completed && summary.failed_frames == 0U) {
        result = summary.average_fps >= 30.0
                     ? CAMERA_BENCHMARK_PASS
                     : CAMERA_BENCHMARK_PARTIAL;
    }
    ESP_LOGI(TAG,
             "QQVGA_PCLK_RESULT divisor=%u result=%s fps=%.3f "
             "min_1s=%u max_1s=%u P95=%.3fms failures=%u",
             OV3660_TIMING_PCLK_DIV,
             camera_benchmark_result_name(result),
             summary.average_fps,
             summary.minimum_window_frames,
             summary.maximum_window_frames,
             summary.p95_interval_ms,
             summary.failed_frames);
    return result;
}
