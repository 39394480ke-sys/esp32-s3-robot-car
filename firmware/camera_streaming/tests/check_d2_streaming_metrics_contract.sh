#!/usr/bin/env bash

set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
main_dir="$project_dir/main"

for required in stream_metrics.c stream_metrics.h stream_server.c stream_server.h; do
  test -f "$main_dir/$required" || {
    echo "missing D2 Phase C file: $required" >&2
    exit 1
  }
done

for metric in \
  d1_camera_fps \
  stream_session_id \
  stream_session_active \
  stream_elapsed_s \
  camera_frames \
  camera_failures \
  invalid_frames \
  header_warmup_frames \
  header_mismatch_frames \
  camera_fps \
  avg_camera_wait_ms \
  frames_sent \
  stream_fps \
  bytes_sent \
  throughput_bps \
  avg_jpeg_size \
  avg_send_ms \
  p95_send_ms \
  send_failures \
  last_send_error \
  internal_heap_free \
  internal_heap_min \
  psram_free \
  psram_min; do
  grep -Fq "\"$metric\"" "$main_dir/stream_server.c" || {
    echo "missing D2 Phase C status metric: $metric" >&2
    exit 1
  }
done

for contract in \
  stream_metrics_begin_session \
  stream_metrics_end_session \
  stream_metrics_record_camera_frame \
  stream_metrics_record_camera_failure \
  stream_metrics_record_send_success \
  stream_metrics_record_send_failure \
  heap_caps_get_minimum_free_size; do
  grep -Fq "$contract" "$main_dir/stream_server.c" "$main_dir/stream_metrics.c" || {
    echo "missing D2 Phase C metrics contract: $contract" >&2
    exit 1
  }
done

if grep -Eq 'malloc|calloc|realloc' "$main_dir/stream_metrics.c"; then
  echo "D2 Phase C metrics must use fixed memory" >&2
  exit 1
fi

echo "D2 streaming metrics contract checks passed"
