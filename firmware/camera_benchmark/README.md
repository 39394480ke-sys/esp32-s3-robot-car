# ESP32-S3 Camera Benchmark

This standalone ESP-IDF project contains the D0 hardware probe and D1
camera-only benchmark. It never initializes Wi-Fi, motors, or other robot
peripherals.

## D0 probe

The probe reports the ESP-IDF and application build, chip model and revision,
CPU core count, detected Flash size, internal heap, PSRAM initialization and
capacity, PSRAM heap, and a 256 KiB PSRAM allocation/read/write check.

The board baseline is configured for 16 MB DIO Flash and 8 MB 40 MHz Octal
PSRAM. The 40 MHz PSRAM setting is intentional: the physical board repeatedly
failed ESP-IDF's boot memory test at 80 MHz. Build and flash it from an
initialized ESP-IDF shell:

The project pins Espressif's `esp32-camera` component to 2.1.4. This release
includes the ESP32-S3 GDMA deinitialization fix needed by the safety bring-up.

The D1 baseline uses OV3660 QVGA JPEG, quality 20, 20 MHz XCLK, two PSRAM frame
buffers, latest-frame grab mode, and PSRAM DMA disabled. It discards 30 warm-up
frames, then measures real framebuffer acquisitions for 60 seconds and reports
one-second windows, JPEG sizes, frame intervals, failures, and heap snapshots.

```sh
idf.py set-target esp32s3
idf.py build
idf.py -p PORT flash monitor
```
