#!/usr/bin/env bash

set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
test_binary="${TMPDIR:-/tmp}/camera_benchmark_statistics_test"

cc -std=c11 -Wall -Wextra -Werror -pedantic \
  -I "$project_dir/main" \
  "$project_dir/tests/benchmark_statistics_test.c" \
  "$project_dir/main/benchmark_statistics.c" \
  -o "$test_binary"

"$test_binary"
echo "benchmark statistics host tests passed"
