#!/usr/bin/env bash

set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
benchmark="$project_dir/main/camera_benchmark.c"
camera_init="$project_dir/main/camera_init.c"

for contract in \
  'PSRAM_DMA_DIAGNOSTIC_DURATION_SECONDS 15U' \
  'false, true' \
  camera_run_psram_dma_diagnostic \
  esp_camera_set_psram_mode \
  esp_camera_get_psram_mode \
  PSRAM_DMA_DIAGNOSTIC_RESULT; do
  grep -q "$contract" "$benchmark" "$camera_init" || {
    echo "missing PSRAM DMA diagnostic contract: $contract" >&2
    exit 1
  }
done

echo "D1 PSRAM DMA diagnostic contract checks passed"
