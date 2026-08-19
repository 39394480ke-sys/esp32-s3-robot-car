#!/usr/bin/env bash

set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
main_dir="$project_dir/main"

for required in \
  CMakeLists.txt \
  sdkconfig.defaults \
  main/CMakeLists.txt \
  main/idf_component.yml \
  main/app_main.c \
  main/camera_init.c \
  main/camera_board_config.h; do
  test -f "$project_dir/$required" || {
    echo "missing D2 streaming project file: $required" >&2
    exit 1
  }
done

for camera_contract in \
  'xclk_freq_hz = 20000000' \
  'pixel_format = PIXFORMAT_JPEG' \
  'frame_size = FRAMESIZE_QVGA' \
  'jpeg_quality = 20' \
  'fb_count = 2' \
  'fb_location = CAMERA_FB_IN_PSRAM' \
  'grab_mode = CAMERA_GRAB_LATEST' \
  'esp_camera_set_psram_mode(false)'; do
  grep -q "$camera_contract" "$main_dir/camera_init.c" || {
    echo "missing D2 streaming Camera contract: $camera_contract" >&2
    exit 1
  }
done

grep -q '^CONFIG_SPIRAM_SPEED_40M=y$' "$project_dir/sdkconfig.defaults"
grep -q '^CONFIG_CAMERA_JPEG_MODE_FRAME_SIZE_CUSTOM=y$' "$project_dir/sdkconfig.defaults"
grep -q '^CONFIG_CAMERA_JPEG_MODE_FRAME_SIZE=32768$' "$project_dir/sdkconfig.defaults"
if grep -q '^CONFIG_CAMERA_JPEG_MODE_FRAME_SIZE_AUTO=y$' "$project_dir/sdkconfig.defaults"; then
  echo "automatic QVGA JPEG buffer is only 15360 bytes and can overflow" >&2
  exit 1
fi

if grep -q 'set_pll' "$main_dir/camera_init.c"; then
  echo "D2 streaming must retain the frozen OV3660 default PCLK" >&2
  exit 1
fi

if grep -ERq 'camera_run_(quality|fb_count|psram_dma|qqvga)|FRAMESIZE_QQVGA|camera_apply_ov3660_pclk_div' "$main_dir"; then
  echo "D1 Camera diagnostics leaked into the D2 streaming default path" >&2
  exit 1
fi

if grep -ERq 'FRAMESIZE_QQVGA|fmt2jpg|frame2jpg' "$main_dir"; then
  echo "D2 streaming must preserve the direct QVGA JPEG baseline" >&2
  exit 1
fi

echo "D2 streaming project contract checks passed"
