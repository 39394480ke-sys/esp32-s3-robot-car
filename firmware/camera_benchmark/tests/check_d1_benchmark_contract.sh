#!/usr/bin/env bash

set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source_file="$project_dir/main/camera_benchmark.c"
header_file="$project_dir/main/camera_benchmark.h"

test -f "$source_file" && test -f "$header_file" || {
  echo "missing camera benchmark module" >&2
  exit 1
}

for contract in \
  'BENCHMARK_WARMUP_FRAMES 30U' \
  'BENCHMARK_DURATION_SECONDS 60U' \
  esp_timer_get_time \
  esp_camera_fb_get \
  esp_camera_fb_return \
  benchmark_statistics_record_frame \
  benchmark_statistics_record_failure \
  heap_caps_get_free_size \
  heap_caps_get_minimum_free_size \
  heap_caps_get_largest_free_block \
  D1_BENCHMARK_RESULT \
  average_fps \
  minimum_window_frames \
  p50_interval_ms \
  p95_interval_ms; do
  grep -q "$contract" "$source_file" "$header_file" || {
    echo "missing D1 benchmark contract: $contract" >&2
    exit 1
  }
done

echo "D1 benchmark contract checks passed"
