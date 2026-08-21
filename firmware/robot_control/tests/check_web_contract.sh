#!/usr/bin/env bash

set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
server_file="$project_dir/main/web_server.c"
page_file="$project_dir/main/web_page.c"
wifi_file="$project_dir/main/wifi_manager.c"

for route in \
  '"/"' \
  '"/status"' \
  '"/api/drive"' \
  '"/api/stop"' \
  '"/api/estop"' \
  '"/api/estop/clear"'; do
  grep -q "$route" "$server_file" || {
    echo "missing Web route: $route" >&2
    exit 1
  }
done

grep -q '"/api/servo"' "$server_file" || {
  echo "missing Web route: /api/servo" >&2
  exit 1
}

grep -q '"/api/oled"' "$server_file" || {
  echo "missing Web route: /api/oled" >&2
  exit 1
}

grep -q '"/api/tts"' "$server_file" || {
  echo "missing Web route: /api/tts" >&2
  exit 1
}

grep -q 'robot_state_get_full_snapshot' "$server_file" || {
  echo "status must read the unified robot snapshot" >&2
  exit 1
}

status_handler="$(sed -n '/static esp_err_t status_handler/,/static bool parse_command/p' "$server_file")"
for legacy_getter in \
  wifi_manager_get_snapshot \
  robot_state_get_snapshot \
  servo_control_get_snapshot \
  oled_ui_get_snapshot \
  tts_control_get_snapshot; do
  if grep -q "$legacy_getter" <<<"$status_handler"; then
    echo "status bypasses unified state with: $legacy_getter" >&2
    exit 1
  fi
done

for status_field in \
  robot_id uptime wifi rssi motion speed estop yaw pitch expression tts_busy; do
  grep -q "\"$status_field\"" "$server_file" || {
    echo "missing /status field: $status_field" >&2
    exit 1
  }
done

for gimbal_contract in \
  'id=\\"yaw\\"' \
  'id=\\"pitch\\"' \
  '/api/servo?axis='; do
  grep -q "$gimbal_contract" "$page_file" || {
    echo "missing gimbal Web contract: $gimbal_contract" >&2
    exit 1
  }
done

for phrase in hello here received stopped; do
  grep -Fq "data-phrase=\\\"$phrase\\\"" "$page_file" || {
    echo "missing TTS phrase control: $phrase" >&2
    exit 1
  }
done

for expression in idle happy curious confused sleepy watching warning excited; do
  grep -Fq "data-expression=\\\"$expression\\\"" "$page_file" || {
    echo "missing OLED expression control: $expression" >&2
    exit 1
  }
done

for contract in \
  'setInterval.*200' \
  'pointerup' \
  'pointercancel' \
  'visibilitychange' \
  'sendBeacon' \
  'KeyW' \
  'ArrowUp' \
  'Space'; do
  grep -q "$contract" "$page_file" || {
    echo "missing browser safety contract: $contract" >&2
    exit 1
  }
done

disconnect_line="$(grep -n 'robot_notify_wifi_disconnected' "$wifi_file" | head -1 | cut -d: -f1)"
reconnect_line="$(grep -n 'connect_station();' "$wifi_file" | tail -1 | cut -d: -f1)"
test "$disconnect_line" -lt "$reconnect_line" || {
  echo "Wi-Fi disconnect must stop the robot before reconnecting" >&2
  exit 1
}

grep -q 'default ""' "$project_dir/main/Kconfig.projbuild"
git -C "$project_dir" check-ignore -q sdkconfig.local || {
  echo "sdkconfig.local containing Wi-Fi credentials must be ignored" >&2
  exit 1
}

echo "Web and Wi-Fi contract checks passed"
