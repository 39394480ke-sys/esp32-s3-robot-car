# D2 PC MJPEG Receiver

`pc_mjpeg_receiver.py` is the no-display benchmark receiver for D2. It connects
directly to the robot rather than using environment HTTP proxies, parses each
multipart part by `Content-Length`, and verifies JPEG SOI/EOI markers without
decoding or saving the image.

Run a short measurement and write the same result as JSON:

```sh
tools/pc_mjpeg_receiver.py \
  --url http://ROBOT_IP/stream \
  --warmup-seconds 5 \
  --duration 60 \
  --json-out /tmp/d2-receiver-report.json
```

The timed window starts after warm-up. `frames_received` counts complete JPEG
parts in that window, and `bytes_received` contains JPEG payload bytes only.
One-second FPS values contain complete one-second windows. Interarrival
percentiles use the same nearest-rank definition as the Camera benchmark, and
`interarrival_over_200ms` counts intervals strictly greater than 200 ms.
`relative_latency_drift_ms` compares elapsed PC arrival time with elapsed
capture time, so clock synchronization is not required. Sustained positive
drift indicates accumulating transport latency. The largest positive value is
reported as `max_relative_latency_growth_ms`.

Run the host tests with:

```sh
tools/tests/run_host_tests.sh
```
