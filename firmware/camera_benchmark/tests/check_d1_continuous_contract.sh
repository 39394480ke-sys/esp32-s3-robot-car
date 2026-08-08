#!/usr/bin/env bash

set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source_file="$project_dir/main/camera_safety_check.c"
header_file="$project_dir/main/camera_safety_check.h"
app_file="$project_dir/main/app_main.c"

grep -Eq '#define[[:space:]]+CONTINUOUS_FRAME_COUNT[[:space:]]+60U' "$source_file" || {
  echo "continuous safety check must capture exactly 60 frames" >&2
  exit 1
}

for behavior in \
  camera_run_continuous_safety_check \
  esp_camera_fb_get \
  esp_camera_fb_return \
  successful_frames \
  failed_frames \
  D1_CONTINUOUS_RESULT; do
  grep -q "$behavior" "$source_file" "$header_file" "$app_file" || {
    echo "missing continuous safety behavior: $behavior" >&2
    exit 1
  }
done

grep -q 'camera_shutdown' "$source_file" || {
  echo "continuous safety check must deinitialize the camera" >&2
  exit 1
}

echo "D1 continuous safety contract checks passed"
