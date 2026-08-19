#!/usr/bin/env bash

set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
kconfig="$project_dir/main/Kconfig.projbuild"
defaults="$project_dir/sdkconfig.defaults"
ignore_file="$project_dir/.gitignore"

test -f "$kconfig" && test -f "$defaults" && test -f "$ignore_file" || {
  echo "missing D2 local configuration contract" >&2
  exit 1
}

for symbol in ROBOT_ID ROBOT_WIFI_SSID ROBOT_WIFI_PASSWORD D2_WIFI_RECONNECT_SELF_TEST; do
  grep -q "config $symbol" "$kconfig" || {
    echo "missing D2 Kconfig symbol: $symbol" >&2
    exit 1
  }
done

grep -q 'default "robot-01"' "$kconfig" || {
  echo "robot_id does not default to robot-01" >&2
  exit 1
}

if grep -Eq '^CONFIG_ROBOT_WIFI_(SSID|PASSWORD)=' "$defaults"; then
  echo "real Wi-Fi credentials must not be present in tracked defaults" >&2
  exit 1
fi

for ignored in build/ managed_components/ sdkconfig sdkconfig.local; do
  grep -qx "$ignored" "$ignore_file" || {
    echo "D2 local artifact is not ignored: $ignored" >&2
    exit 1
  }
done

if grep -ERqi 'log.*password|password.*log|password=%' "$project_dir/main"; then
  echo "Wi-Fi password must not be logged" >&2
  exit 1
fi

echo "D2 credential isolation contract checks passed"
