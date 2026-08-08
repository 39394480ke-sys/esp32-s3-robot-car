#!/usr/bin/env bash

set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
benchmark="$project_dir/main/camera_benchmark.c"
camera_init="$project_dir/main/camera_init.c"
app="$project_dir/main/app_main.c"

for contract in \
  'FRAMESIZE_QVGA' \
  'FRAMESIZE_QQVGA' \
  'QQVGA_DIAGNOSTIC_DURATION_SECONDS 15U' \
  'QQVGA_WIDTH 160U' \
  'QQVGA_HEIGHT 120U' \
  'camera_run_qqvga_short_diagnostic' \
  'QQVGA_SHORT_RESULT'; do
  grep -q "$contract" "$benchmark" "$camera_init" "$app" || {
    echo "missing QQVGA diagnostic contract: $contract" >&2
    exit 1
  }
done

for fixed_setting in \
  'jpeg_quality = 20' \
  'xclk_freq_hz = 20000000' \
  'fb_count = 2' \
  'fb_location = CAMERA_FB_IN_PSRAM' \
  'grab_mode = CAMERA_GRAB_LATEST'; do
  grep -q "$fixed_setting" "$camera_init" || {
    echo "QQVGA diagnostic changed required setting: $fixed_setting" >&2
    exit 1
  }
done

grep -Eq 'camera_run_qqvga_(short|pclk)_diagnostic' "$app" || {
  echo "application does not run a QQVGA diagnostic" >&2
  exit 1
}

echo "D1 QQVGA diagnostic contract checks passed"
