# ESP32-S3 Camera Streaming

> **Archived experiment:** D2 ended as `paused / partial`. This implementation
> did not pass the 60-second performance gate or either 10-minute soak test and
> is no longer maintained as a planned robot feature.

This standalone ESP-IDF project is the D2 Camera, Wi-Fi STA, and single-client
HTTP MJPEG bring-up. It provides:

- `GET /` for the browser viewer and live stream status.
- `GET /status` for the current robot, Wi-Fi, Camera profile, and stream state.
- `GET /stream` for direct OV3660 JPEG frames as multipart MJPEG.

The stream keeps the frozen QVGA, quality 20, XCLK 20 MHz, dual-framebuffer,
LATEST, and PSRAM DMA OFF profile. Its JPEG slots are 32768 bytes because the
driver's 15360-byte automatic QVGA size was observed to overflow. A lightweight
JPEG marker validator drops structurally corrupt Camera frames before HTTP
delivery. The first three matching frames also establish a fixed JPEG-header
fingerprint, so later quantization, Huffman, or color-layout header corruption
is dropped instead of producing a one-frame color flash. Neither check decodes
or re-encodes the image.

Each multipart frame is assembled in one reusable PSRAM staging buffer and
submitted as one HTTP chunk. The framebuffer is returned before the network
send begins, and the 32768-byte TCP send buffer absorbs normal LAN ACK jitter
without creating an application frame queue. A send blocked for two seconds is
closed and fully released instead of leaving the viewer permanently frozen.

The first stream client is accepted. Additional stream clients receive HTTP
503 until the active client disconnects.

## Streaming metrics

Each accepted `/stream` connection starts a new metrics session. The counters
reset for that connection, remain available after disconnect, and reset again
when the next connection starts. `/status` reports the frozen D1 reference
(`d1_camera_fps=27.683`) together with Camera FPS, fully sent FPS, JPEG payload
bytes and throughput, average Camera wait, average/P95 send time, filtered frame
counts, send failures, and current/minimum internal and PSRAM heap.

`camera_frames` counts framebuffers that pass the QVGA JPEG structural checks.
`frames_sent` counts only frames whose multipart header, JPEG payload, and final
CRLF were all sent successfully. `bytes_sent` is successful JPEG payload only;
it excludes multipart overhead. The fixed-memory P95 histogram has 1 ms buckets
through 100 ms, 10 ms buckets through 1 second, 100 ms buckets through 5
seconds, and a final overflow bucket reported as 5000 ms.

The lightweight validation cannot identify every JPEG entropy error that a
full decoder can expose. `invalid_frames` and `header_mismatch_frames` therefore
describe only frames rejected by the on-device structural and header checks.
The PC receiver measures received FPS, stalls, and relative latency drift, but
the optimized firmware could not complete its hardware performance gate after
the PSRAM initialization failure described in the D2 report.

The tracked defaults preserve the validated QVGA Camera profile and contain no
Wi-Fi credentials. Create an ignored `sdkconfig.local` with local values before
building for hardware:

```text
CONFIG_ROBOT_WIFI_SSID="your-ssid"
CONFIG_ROBOT_WIFI_PASSWORD="your-password"
CONFIG_D2_WIFI_RECONNECT_SELF_TEST=n
```

Build and flash from an initialized ESP-IDF v5.5.3 shell:

```sh
idf.py build
idf.py -p PORT flash monitor
```
