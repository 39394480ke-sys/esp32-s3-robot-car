#!/usr/bin/env bash

set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
benchmark="$project_dir/main/camera_benchmark.c"
header="$project_dir/main/camera_benchmark.h"
statistics="$project_dir/main/benchmark_statistics.h"
app="$project_dir/main/app_main.c"
defaults="$project_dir/sdkconfig.defaults"

for contract in \
  'CAMERA_REFERENCE_SOAK_DURATION_SECONDS 600U' \
  'camera_run_reference_soak' \
  'D2_CAMERA_REFERENCE_SOAK_RESULT' \
  'summary.average_fps >= 26.0'; do
  grep -q "$contract" "$benchmark" "$header" || {
    echo "missing D2 Camera reference soak contract: $contract" >&2
    exit 1
  }
done

grep -q '^#define BENCHMARK_MAX_INTERVALS 20000U$' "$statistics" || {
  echo "D2 soak does not retain enough frame intervals" >&2
  exit 1
}

grep -q '^#define BENCHMARK_MAX_WINDOWS 600U$' "$statistics" || {
  echo "D2 soak does not retain all one-second windows" >&2
  exit 1
}

grep -q 'camera_run_reference_soak' "$app" || {
  echo "application does not run the D2 Camera reference soak" >&2
  exit 1
}

grep -q '^CONFIG_CAMERA_JPEG_MODE_FRAME_SIZE_AUTO=y$' "$defaults" || {
  echo "D2 QVGA reference soak does not use automatic JPEG buffer sizing" >&2
  exit 1
}

if grep -q '^CONFIG_CAMERA_JPEG_MODE_FRAME_SIZE_CUSTOM=y$' "$defaults"; then
  echo "D2 QVGA reference soak inherited the QQVGA diagnostic buffer" >&2
  exit 1
fi

echo "D2 Camera reference soak contract checks passed"
