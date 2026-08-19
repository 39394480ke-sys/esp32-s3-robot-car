#!/usr/bin/env bash

set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
defaults="$project_dir/sdkconfig.defaults"
camera_init="$project_dir/main/camera_init.c"
app="$project_dir/main/app_main.c"

grep -q '^CONFIG_CAMERA_JPEG_MODE_FRAME_SIZE_CUSTOM=y$' "$defaults" || {
  echo "QQVGA custom JPEG frame buffer mode is not selected" >&2
  exit 1
}

grep -q '^CONFIG_CAMERA_JPEG_MODE_FRAME_SIZE=8192$' "$defaults" || {
  echo "QQVGA custom JPEG frame buffer size is not 8192 bytes" >&2
  exit 1
}

if grep -q '^CONFIG_CAMERA_JPEG_MODE_FRAME_SIZE_AUTO=y$' "$defaults"; then
  echo "automatic JPEG frame buffer sizing must not be selected" >&2
  exit 1
fi

grep -Eq 'camera_run_qqvga_(short|pclk)_diagnostic' "$app" || {
  echo "application does not run a QQVGA diagnostic" >&2
  exit 1
}

grep -q 'camera_init_with_frame_settings(.*' "$project_dir/main/camera_benchmark.c" || {
  echo "QQVGA diagnostic is not using the established camera initialization path" >&2
  exit 1
}

grep -q 'psram_dma ? "ON" : "OFF"' "$camera_init" || {
  echo "PSRAM DMA state is not logged and verifiable" >&2
  exit 1
}

echo "D1 QQVGA custom JPEG buffer contract checks passed"
