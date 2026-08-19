#!/usr/bin/env bash

set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
test_binary="${TMPDIR:-/tmp}/camera_streaming_protocol_test"
jpeg_test_binary="${TMPDIR:-/tmp}/camera_streaming_jpeg_validator_test"
metrics_test_binary="${TMPDIR:-/tmp}/camera_streaming_metrics_test"

cc -std=c11 -Wall -Wextra -Werror -pedantic \
  -I "$project_dir/main" \
  "$project_dir/tests/stream_protocol_test.c" \
  "$project_dir/main/stream_protocol.c" \
  -o "$test_binary"

"$test_binary"

cc -std=c11 -Wall -Wextra -Werror -pedantic \
  -I "$project_dir/main" \
  "$project_dir/tests/jpeg_validator_test.c" \
  "$project_dir/main/jpeg_validator.c" \
  -o "$jpeg_test_binary"

"$jpeg_test_binary"

cc -std=c11 -Wall -Wextra -Werror -pedantic \
  -I "$project_dir/main" \
  "$project_dir/tests/stream_metrics_test.c" \
  "$project_dir/main/stream_metrics.c" \
  -lm \
  -o "$metrics_test_binary"

"$metrics_test_binary"
echo "stream protocol host tests passed"
