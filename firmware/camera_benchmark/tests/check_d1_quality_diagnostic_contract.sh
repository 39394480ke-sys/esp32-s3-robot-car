#!/usr/bin/env bash

set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
benchmark="$project_dir/main/camera_benchmark.c"
camera_init="$project_dir/main/camera_init.c"

for contract in \
  'QUALITY_DIAGNOSTIC_DURATION_SECONDS 15U' \
  '12U, 20U, 30U, 40U' \
  camera_run_quality_diagnostic \
  camera_init_with_quality \
  QUALITY_DIAGNOSTIC_RESULT; do
  grep -q "$contract" "$benchmark" "$camera_init" || {
    echo "missing JPEG quality diagnostic contract: $contract" >&2
    exit 1
  }
done

echo "D1 JPEG quality diagnostic contract checks passed"
