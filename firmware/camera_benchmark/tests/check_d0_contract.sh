#!/usr/bin/env bash

set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

required_files=(
  "CMakeLists.txt"
  "sdkconfig.defaults"
  "main/CMakeLists.txt"
  "main/app_main.c"
  "main/hardware_info.c"
  "main/hardware_info.h"
)

for file in "${required_files[@]}"; do
  test -f "$project_dir/$file" || {
    echo "missing required file: $file" >&2
    exit 1
  }
done

config="$project_dir/sdkconfig.defaults"
grep -qx 'CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y' "$config"
grep -qx 'CONFIG_SPIRAM=y' "$config"
grep -qx 'CONFIG_SPIRAM_MODE_OCT=y' "$config"
grep -qx 'CONFIG_SPIRAM_SPEED_40M=y' "$config"
grep -qx 'CONFIG_SPIRAM_SPEED=40' "$config"
grep -qx 'CONFIG_SPIRAM_BOOT_INIT=y' "$config"
grep -qx 'CONFIG_SPIRAM_USE_MALLOC=y' "$config"
grep -qx 'CONFIG_SPIRAM_MEMTEST=y' "$config"

grep -q 'esp_app_format' "$project_dir/main/CMakeLists.txt" || {
  echo "main component must declare esp_app_format for esp_app_desc.h" >&2
  exit 1
}

source_files=("$project_dir/main/app_main.c" "$project_dir/main/hardware_info.c")
source_text="$(cat "${source_files[@]}")"

for required_symbol in \
  esp_chip_info \
  esp_flash_get_size \
  esp_psram_is_initialized \
  esp_psram_get_size \
  heap_caps_get_free_size \
  heap_caps_get_minimum_free_size \
  heap_caps_get_largest_free_block \
  MALLOC_CAP_SPIRAM; do
  grep -q "$required_symbol" <<<"$source_text" || {
    echo "missing D0 probe symbol: $required_symbol" >&2
    exit 1
  }
done

if grep -Eq '#include[[:space:]]*[<"](esp_camera|esp_wifi|motor)' "${source_files[@]}"; then
  echo "D0 probe must not initialize camera, Wi-Fi, or motors" >&2
  exit 1
fi

echo "D0 contract checks passed"
