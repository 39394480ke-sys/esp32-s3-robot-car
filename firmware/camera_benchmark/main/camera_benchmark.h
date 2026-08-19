#pragma once

#include <stdbool.h>

typedef enum {
    CAMERA_BENCHMARK_FAIL = 0,
    CAMERA_BENCHMARK_PARTIAL,
    CAMERA_BENCHMARK_PASS,
} camera_benchmark_result_t;

camera_benchmark_result_t camera_run_baseline_benchmark(void);
bool camera_run_quality_diagnostic(void);
bool camera_run_fb_count_diagnostic(void);
bool camera_run_psram_dma_diagnostic(void);
camera_benchmark_result_t camera_run_qqvga_short_diagnostic(void);
camera_benchmark_result_t camera_run_qqvga_formal_benchmark(void);
camera_benchmark_result_t camera_run_qqvga_pclk_diagnostic(void);
const char *camera_benchmark_result_name(camera_benchmark_result_t result);
