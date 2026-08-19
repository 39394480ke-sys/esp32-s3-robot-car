#!/usr/bin/env bash

set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
main_dir="$project_dir/main"

for required in \
  stream_protocol.c \
  stream_protocol.h \
  jpeg_validator.c \
  jpeg_validator.h \
  stream_server.c \
  stream_server.h; do
  test -f "$main_dir/$required" || {
    echo "missing D2 Phase B file: $required" >&2
    exit 1
  }
done

for contract in \
  stream_server_start \
  stream_server_get_snapshot \
  httpd_req_async_handler_begin \
  httpd_req_async_handler_complete \
  httpd_resp_send_chunk \
  TCP_NODELAY \
  VIEWER_HTML \
  heap_caps_malloc \
  MALLOC_CAP_SPIRAM \
  esp_camera_fb_get \
  esp_camera_fb_return \
  camera_frame_is_valid \
  'STREAM_HEADER_CONSENSUS_FRAMES 3U' \
  'header fingerprint mismatch' \
  'send_wait_timeout = 2U' \
  '503 Service Unavailable' \
  '"/"' \
  '/stream' \
  '/status'; do
  grep -Fq "$contract" "$main_dir/stream_server.c" "$main_dir/stream_server.h" || {
    echo "missing D2 HTTP streaming contract: $contract" >&2
    exit 1
  }
done

grep -q 'CONFIG_LWIP_TCP_SND_BUF_DEFAULT=32768' "$project_dir/sdkconfig.defaults" || {
  echo "streaming TCP send buffer must be 32768 bytes" >&2
  exit 1
}

test "$(grep -c 'httpd_resp_send_chunk(' "$main_dir/stream_server.c")" -eq 1 || {
  echo "each MJPEG part must use one chunk send call" >&2
  exit 1
}

grep -Fq 'jpeg_data_is_structurally_valid' \
  "$main_dir/jpeg_validator.c" "$main_dir/stream_server.c" || {
  echo "missing structural JPEG validation contract" >&2
  exit 1
}

for header in \
  'multipart/x-mixed-replace; boundary=d2frame' \
  'Content-Type: image/jpeg' \
  'Content-Length:' \
  'X-Frame-Id:' \
  'X-Capture-Timestamp-Us:'; do
  grep -Fq "$header" "$main_dir/stream_protocol.c" "$main_dir/stream_protocol.h" || {
    echo "missing D2 MJPEG protocol contract: $header" >&2
    exit 1
  }
done

if grep -ERq 'FRAMESIZE_QQVGA|fmt2jpg|frame2jpg|xQueue(Create|Send).*frame' "$main_dir"; then
  echo "D2 stream must send direct QVGA JPEG without a frame backlog" >&2
  exit 1
fi

echo "D2 HTTP streaming contract checks passed"
