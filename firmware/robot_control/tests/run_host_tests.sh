#!/usr/bin/env bash

set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
test_binary="${TMPDIR:-/tmp}/robot_control_host_test"

grep -q '^CONFIG_IDF_TARGET="esp32s3"$' "$project_dir/sdkconfig.defaults"
grep -q '^set(COMPONENTS main)$' "$project_dir/CMakeLists.txt"
if grep -ERq '#include[[:space:]]*[<"](esp_camera|esp_psram|driver/pcnt)' \
  "$project_dir/main"; then
  echo "robot control must not initialize Camera, PSRAM, or encoders" >&2
  exit 1
fi

"$project_dir/tests/check_web_contract.sh"

cc \
  -std=c11 \
  -Wall \
  -Wextra \
  -Werror \
  -I"$project_dir/tests/fakes" \
  -I"$project_dir/main" \
  "$project_dir/main/motor_policy.c" \
  "$project_dir/main/desktop_idle_policy.c" \
  "$project_dir/main/oled_expression.c" \
  "$project_dir/main/servo_policy.c" \
  "$project_dir/main/tts_protocol.c" \
  "$project_dir/main/robot_control.c" \
  "$project_dir/main/robot_state.c" \
  "$project_dir/main/safety.c" \
  "$project_dir/tests/robot_control_host_test.c" \
  -DROBOT_CONTROL_HOST_TEST \
  -o "$test_binary"

"$test_binary"
