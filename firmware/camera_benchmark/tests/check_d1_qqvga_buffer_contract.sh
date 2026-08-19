#!/usr/bin/env bash

set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
defaults="$project_dir/sdkconfig.defaults"
camera_init="$project_dir/main/camera_init.c"
benchmark="$project_dir/main/camera_benchmark.c"

grep -q '^CONFIG_CAMERA_JPEG_MODE_FRAME_SIZE_AUTO=y$' "$defaults" || {
  echo "D2 QVGA baseline does not use automatic JPEG frame sizing" >&2
  exit 1
}

if grep -q '^CONFIG_CAMERA_JPEG_MODE_FRAME_SIZE_CUSTOM=y$' "$defaults"; then
  echo "QQVGA diagnostic buffer must not replace the D2 QVGA baseline" >&2
  exit 1
fi

grep -Eq 'camera_run_qqvga_(short|pclk)_diagnostic' "$benchmark" || {
  echo "QQVGA diagnostic is not retained" >&2
  exit 1
}

grep -q 'camera_init_with_frame_settings(.*' "$benchmark" || {
  echo "QQVGA diagnostic is not using the established camera initialization path" >&2
  exit 1
}

grep -q 'psram_dma ? "ON" : "OFF"' "$camera_init" || {
  echo "PSRAM DMA state is not logged and verifiable" >&2
  exit 1
}

echo "D1 QQVGA diagnostic retained; D2 QVGA buffer baseline checks passed"
