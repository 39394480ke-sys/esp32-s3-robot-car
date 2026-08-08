#!/usr/bin/env bash

set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
benchmark="$project_dir/main/camera_benchmark.c"
camera_init="$project_dir/main/camera_init.c"

for contract in \
  'FB_COUNT_DIAGNOSTIC_DURATION_SECONDS 15U' \
  '1U, 2U' \
  camera_run_fb_count_diagnostic \
  camera_init_with_settings \
  FB_COUNT_DIAGNOSTIC_RESULT; do
  grep -q "$contract" "$benchmark" "$camera_init" || {
    echo "missing framebuffer-count diagnostic contract: $contract" >&2
    exit 1
  }
done

echo "D1 framebuffer-count diagnostic contract checks passed"
