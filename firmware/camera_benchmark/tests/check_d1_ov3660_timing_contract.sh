#!/usr/bin/env bash

set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
benchmark="$project_dir/main/camera_benchmark.c"
camera_init="$project_dir/main/camera_init.c"
app="$project_dir/main/app_main.c"

for contract in \
  'OV3660_TIMING_PCLK_DIV 9U' \
  'OV3660_PCLK_RATIO_REG 0x3824' \
  'camera_apply_ov3660_pclk_div' \
  'sensor->set_pll' \
  'sensor->get_reg' \
  'OV3660_PCLK_DIV_VERIFIED' \
  'camera_run_qqvga_pclk_diagnostic' \
  'QQVGA_PCLK_RESULT'; do
  grep -q "$contract" "$benchmark" "$camera_init" "$app" || {
    echo "missing OV3660 timing diagnostic contract: $contract" >&2
    exit 1
  }
done

for fixed_setting in \
  'FRAMESIZE_QQVGA' \
  '20U' \
  '2U' \
  'false'; do
  grep -q "$fixed_setting" "$benchmark" || {
    echo "OV3660 timing diagnostic changed a fixed benchmark setting: $fixed_setting" >&2
    exit 1
  }
done

grep -q 'frame->buf\[frame->len - 2U\] == 0xffU' "$benchmark" || {
  echo "timing diagnostic does not verify JPEG EOI integrity" >&2
  exit 1
}

echo "D1 OV3660 PCLK timing contract checks passed"
