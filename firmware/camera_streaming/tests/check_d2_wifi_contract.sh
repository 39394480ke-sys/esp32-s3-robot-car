#!/usr/bin/env bash

set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source_file="$project_dir/main/wifi_manager.c"
header_file="$project_dir/main/wifi_manager.h"
app_file="$project_dir/main/app_main.c"

test -f "$source_file" && test -f "$header_file" && test -f "$app_file" || {
  echo "missing D2 Wi-Fi manager" >&2
  exit 1
}

for contract in \
  wifi_manager_init \
  wifi_manager_wait_for_ip \
  wifi_manager_get_snapshot \
  wifi_manager_disconnect_for_test \
  WIFI_EVENT_STA_START \
  WIFI_EVENT_STA_DISCONNECTED \
  IP_EVENT_STA_GOT_IP \
  esp_netif_create_default_wifi_sta \
  WIFI_MODE_STA \
  esp_wifi_connect \
  esp_wifi_set_ps \
  WIFI_PS_NONE \
  esp_wifi_get_ps \
  esp_wifi_sta_get_ap_info; do
  grep -q "$contract" "$source_file" "$header_file" || {
    echo "missing D2 Wi-Fi contract: $contract" >&2
    exit 1
  }
done

for metric in \
  robot_id \
  connected \
  ip \
  rssi \
  disconnect_count \
  reconnect_count \
  connected_since_seconds \
  power_save; do
  grep -q "$metric" "$header_file" "$app_file" || {
    echo "missing D2 Wi-Fi metric: $metric" >&2
    exit 1
  }
done

grep -q 'D2_WIFI_RECONNECT_SELF_TEST' "$app_file" || {
  echo "missing controlled Wi-Fi reconnect diagnostic" >&2
  exit 1
}

echo "D2 Wi-Fi contract checks passed"
