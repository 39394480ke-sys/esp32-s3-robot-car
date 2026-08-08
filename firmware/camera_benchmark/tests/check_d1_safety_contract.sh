#!/usr/bin/env bash

set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

required_files=(
  "main/camera_board_config.h"
  "main/camera_init.c"
  "main/camera_init.h"
  "main/camera_safety_check.c"
  "main/camera_safety_check.h"
  "main/idf_component.yml"
)

for file in "${required_files[@]}"; do
  test -f "$project_dir/$file" || {
    echo "missing D1 safety file: $file" >&2
    exit 1
  }
done

board_config="$project_dir/main/camera_board_config.h"
expected_pins=(
  'CAM_PIN_PWDN -1' 'CAM_PIN_RESET -1'
  'CAM_PIN_SIOD 4' 'CAM_PIN_SIOC 5'
  'CAM_PIN_VSYNC 6' 'CAM_PIN_HREF 7'
  'CAM_PIN_XCLK 15' 'CAM_PIN_PCLK 13'
  'CAM_PIN_D0 11' 'CAM_PIN_D1 9' 'CAM_PIN_D2 8' 'CAM_PIN_D3 10'
  'CAM_PIN_D4 12' 'CAM_PIN_D5 18' 'CAM_PIN_D6 17' 'CAM_PIN_D7 16'
)
for pin in "${expected_pins[@]}"; do
  grep -Eq "#define[[:space:]]+$pin$" "$board_config" || {
    echo "missing or incorrect camera pin: $pin" >&2
    exit 1
  }
done

source_text="$(cat "$project_dir/main/camera_init.c" "$project_dir/main/camera_safety_check.c")"
grep -q 'espressif/esp32-camera: "2.1.4"' "$project_dir/main/idf_component.yml" || {
  echo "D1 requires esp32-camera 2.1.4 with the ESP32-S3 GDMA deinit fix" >&2
  exit 1
}
for setting in \
  'xclk_freq_hz = 20000000' \
  'pixel_format = PIXFORMAT_JPEG' \
  'frame_size = FRAMESIZE_QVGA' \
  'jpeg_quality = 20' \
  'fb_count = 2' \
  'fb_location = CAMERA_FB_IN_PSRAM' \
  'grab_mode = CAMERA_GRAB_LATEST'; do
  grep -q "$setting" <<<"$source_text" || {
    echo "missing D1 baseline setting: $setting" >&2
    exit 1
  }
done

for behavior in \
  esp_camera_init \
  esp_camera_sensor_get \
  OV3660_PID \
  esp_camera_fb_get \
  esp_camera_fb_return \
  esp_camera_deinit; do
  grep -q "$behavior" <<<"$source_text" || {
    echo "missing D1 safety behavior: $behavior" >&2
    exit 1
  }
done

if grep -Eq '#include[[:space:]]*[<"](esp_wifi|motor)' "$project_dir/main"/*.c; then
  echo "D1 safety firmware must not initialize Wi-Fi or motors" >&2
  exit 1
fi

echo "D1 safety contract checks passed"
