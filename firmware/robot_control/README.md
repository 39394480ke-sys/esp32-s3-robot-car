# Stage A Robot Control + Stage B Desktop Idle

This is the unified ESP-IDF firmware foundation for Stage A and the bounded
Stage B Desktop Idle increment. The firmware implements the frozen motor
wiring, movement capability API, safe
stop behavior, an 800 ms command watchdog, Wi-Fi STA, Web control, and a
runtime-latched emergency stop. It also includes bounded two-axis gimbal
control, reusable look/gesture capability APIs, and SSD1309 expression
rendering. Fixed UTF-8 phrases can be sent to the existing UART TTS module.
When enabled, Desktop Idle starts after 30 seconds without a user command and
adds low-frequency expression changes and small center-relative gimbal motion.

## Implemented boundary

- ESP32-S3 target using ESP-IDF 5.5.3
- TB6612 open-loop motor control at 20 kHz, 10-bit PWM
- `STOP`, `FORWARD`, `BACKWARD`, `TURN_LEFT`, and `TURN_RIGHT`
- speed input from 0 through 100 percent; the Web UI default is 40 percent
- immediate software stop on explicit STOP, invalid input, zero speed, driver
  error, Wi-Fi disconnect notification, or command timeout
- Wi-Fi reconnect with IP, RSSI, disconnect, and reconnect status
- touch, mouse, WASD, arrow-key, and Space control from one responsive page
- runtime E-stop that rejects movement until an explicit Clear
- 50 Hz SG90 control on GPIO1/GPIO2 with requested angles clamped to
  conservative configured bounds
- Web sliders for direct Yaw and Pitch control
- Web Pitch commands are throttled to the latest position every 40 milliseconds
  so continuous dragging remains responsive without irregular debounce jumps
- eight vector-rendered OLED expressions selected through one capability API
- four fixed TTS phrases over UART1 at 9600 baud on GPIO38/GPIO39
- one unified robot snapshot containing uptime, Wi-Fi, motion, safety, gimbal,
  OLED, TTS, mode, and Desktop Idle state; `/status` reads only this aggregation
  boundary
- `MANUAL` and `DESKTOP_IDLE` modes, with Desktop Idle enabled by default
- automatic Idle entry after 30 seconds without a Web command while stopped
- Idle-only OLED changes among `idle`, `curious`, `sleepy`, and `watching`
  every 20–60 seconds
- Idle gimbal gestures every 15–45 seconds, limited to center-relative
  10-degree Yaw or 8-degree upward Pitch motion and a 1.5-second hold
- synchronous manual cancellation that returns an active Idle gesture to the
  configured center before the requested manual command proceeds
- E-stop cancellation that holds the current servo position and prevents new
  Idle entries and gestures while E-stop remains active
- a Web Desktop Idle ON/OFF control plus current mode and Idle action display

The firmware boots with both PWM outputs at zero and all four TB6612 direction
signals low. `app_main` does not issue any movement command.

## Frozen hardware boundary

| Side | PWM | IN1 | IN2 |
|---|---:|---:|---:|
| Left | GPIO40 | GPIO41 | GPIO42 |
| Right | GPIO45 | GPIO46 | GPIO48 |

| Gimbal axis | PWM | Minimum | Center | Maximum |
|---|---:|---:|---:|---:|
| Yaw | GPIO1 | 45 degrees | 90 degrees | 135 degrees |
| Pitch | GPIO2 | 60 degrees | 90 degrees | 120 degrees |

TB6612 `STBY` remains connected directly to 3V3. No wiring changes are part of
this increment. Logical forward defaults to `IN1=1, IN2=0` on both sides. The
`ROBOT_LEFT_MOTOR_INVERTED` and `ROBOT_RIGHT_MOTOR_INVERTED` Kconfig options can
reverse either side without changing the wiring or capability API. The right
motor option is enabled for the installed vehicle based on its observed wheel
polarity.

The limits are Kconfig values and both axes have independent output inversion
options. They remain deliberately narrower than the nominal SG90 range until
the installed mechanism is verified.

The 128x64 SSD1309 shares GPIO47/GPIO21 with the I2C bus. Normal I2C at 0x3C or
0x3D is attempted first; the installed module does not ACK, so the driver falls
back to its vendor-compatible open-drain bit-bang protocol at 0x3C. The
firmware renders faces from a local framebuffer and does not expose pixel
drawing to callers.

Camera, PSRAM, encoders, and AI are not initialized or implemented here.
`CONFIG_SPIRAM` remains disabled because the installed
encoder signals use GPIO35, GPIO36, and GPIO37, which overlap the board's Octal
PSRAM bus. ESP-IDF pulls the `esp_psram` component transitively with Wi-Fi, but
the application does not enable or initialize it.

## Capability API

The public headers are in `main/`:

```c
esp_err_t robot_control_init(void);
esp_err_t robot_drive(robot_motion_command_t command, uint8_t speed_percent);
esp_err_t robot_move_forward(uint8_t speed_percent);
esp_err_t robot_move_backward(uint8_t speed_percent);
esp_err_t robot_turn_left(uint8_t speed_percent);
esp_err_t robot_turn_right(uint8_t speed_percent);
void robot_stop(void);
void robot_notify_wifi_disconnected(void);
void robot_estop_activate(void);
void robot_estop_clear(void);
bool robot_estop_is_active(void);
robot_state_snapshot_t robot_state_get_snapshot(void);
robot_status_snapshot_t robot_state_get_full_snapshot(void);

esp_err_t desktop_idle_init(void);
esp_err_t desktop_idle_set_enabled(bool enabled);
void desktop_idle_record_user_activity(void);
desktop_idle_snapshot_t desktop_idle_get_snapshot(void);

esp_err_t servo_control_init(void);
esp_err_t servo_set_yaw(int16_t angle);
esp_err_t servo_set_pitch(int16_t angle);
esp_err_t look_center(void);
esp_err_t look_left(void);
esp_err_t look_right(void);
esp_err_t look_up(void);
esp_err_t look_down(void);
esp_err_t nod(void);
esp_err_t shake_head(void);

esp_err_t oled_ui_init(void);
esp_err_t oled_set_expression(const char *expression_name);

esp_err_t tts_control_init(void);
esp_err_t tts_speak(const char *text);
esp_err_t tts_play_phrase(tts_phrase_id_t phrase);
```

Every accepted non-STOP movement command refreshes the watchdog. The Web page
repeats the active command every 200 ms. STOP and error paths disarm the
watchdog; they never preserve a previous movement. E-stop remains active across
page refresh and Wi-Fi reconnect, but intentionally clears after an ESP32
reboot.

## Wi-Fi and HTTP

Create an ignored `sdkconfig.local` before configuring the project:

```text
CONFIG_ROBOT_WIFI_SSID="your-ssid"
CONFIG_ROBOT_WIFI_PASSWORD="your-password"
```

The firmware never logs the configured password. If a generated `sdkconfig`
already exists when credentials change, remove or update that ignored generated
file before rebuilding so ESP-IDF reapplies the local defaults.

The unauthenticated LAN API is:

```text
GET  /
GET  /status
POST /api/drive?command=forward|backward|left|right&speed=0..100
POST /api/stop
POST /api/estop
POST /api/estop/clear
POST /api/servo?axis=yaw|pitch&angle=degrees
POST /api/oled?expression=idle|happy|curious|confused|sleepy|watching|warning|excited
POST /api/tts?phrase=hello|here|received|stopped
POST /api/idle?enabled=true|false
```

An E-stop-blocked drive request returns HTTP 409. Invalid input returns HTTP
400 and stops the motors. `GET /status` includes the stable robot ID, uptime,
Wi-Fi and RSSI, motion and safety state, `mode`, `idle_enabled`, `idle_action`,
gimbal angles, OLED expression, TTS state, and current/minimum free heap.
`tts_busy` means the firmware is
transmitting a command to the TTS module; the installed module does not provide
verified playback-completion feedback.

## Build and host tests

Run from an initialized ESP-IDF 5.5.3 shell:

```sh
cd firmware/robot_control
idf.py build
./tests/run_host_tests.sh
```

The host test uses a fake motor backend. It verifies command routing, direction
policy, speed boundaries, stop reasons, driver failure, Wi-Fi disconnect,
E-stop locking and clearing, and the watchdog boundary at 799/800 ms. The
policy tests also cover the exact 30-second Idle boundary, E-stop/motion gates,
15–45 second delay bounds, and center-relative servo clamping. The protocol
tests cover OLED expression IDs and TW-TTS UTF-8 framing. The contract test
checks the HTTP routes, Desktop Idle fields and control, fixed Web controls,
and browser release safeguards.

The Step 19 combination run exercises all six runtime modules for 30 minutes.
Every ten-second cycle drives at 50 percent with 200 ms keep-alive commands,
changes the OLED expression, moves both servo axes inside their configured
limits, checks `/status`, and explicitly stops. A fixed TTS phrase is sent every
ten cycles. The final cleanup stops the motors, centers the gimbal, and restores
the idle expression.

## Evidence status

- **Verified by repository state:** documented GPIO assignment and the prior
  hardware validation record.
- **User-observed:** both motors and normal vehicle movement worked in earlier
  projects. During this increment, with the wheels raised, movement occurred at
  50 percent and the wheels stopped after each explicit STOP; no movement was
  observed at 20 percent. A later, more specific observation found that the
  software Right command produced Forward and Backward produced Left, indicating
  reversed right-motor polarity. After enabling right-motor inversion, the user
  tested the overall controls and reported no remaining problem.
  The earlier installed gimbal used a 127-degree mechanical Yaw center. During
  a later horn reset, Yaw output was reversed and the position reached by the
  old 104-degree command became the new installed center. A +14-degree trim now
  maps that mechanical position to 90 degrees in the Web/API coordinate system.
  The installed SSD1309 displayed the
  default face, and all eight expressions switched correctly from the Web UI.
  The installed TTS module and speaker clearly played all four fixed Chinese
  phrases.
- **Verified in this increment:** ESP-IDF 5.5.3 build and flash, Wi-Fi
  connection, HTTP status, STOP, E-stop/Clear, E-stop rejection, invalid request
  rejection, and host-side safety behavior. The automated 30-minute combination
  run completed 180 cycles with zero HTTP/control failures, zero resets, and no
  additional Wi-Fi disconnect. Uptime advanced continuously, sampled free heap
  started and ended at about 261.5 KiB (lowest sampled value about 261.2 KiB),
  and the robot finished in STOP with Yaw/Pitch centered and the idle expression.
- **Verified for Stage B by machine output and API state:** ESP-IDF 5.5.3 host
  tests and full firmware build passed; the firmware flashed successfully to
  the connected ESP32-S3. Serial logs recorded automatic `DESKTOP_IDLE` entry
  at about 30.9 seconds and later recorded bounded `LOOK_LEFT` and `LOOK_UP`
  actions. During `LOOK_UP`, a manual OLED request produced an immediate Idle
  cancellation log; `/status` then reported `MANUAL`, action `NONE`, centered
  Yaw/Pitch, and the requested expression. E-stop remained active for about 58
  seconds with no new Idle action in the serial log, while `/status` continued
  to report stopped motors, `MANUAL`, and action `NONE`. Idle OFF and ON API
  requests both succeeded and the disabled state appeared in `/status`.
- **Unverified in this increment:** braking distance under floor load, movement
  from the browser UI, physical watchdog/E-stop stopping, established-link
  Wi-Fi reconnection, the exact motor startup threshold, installed gimbal
  endpoint clearance/direction semantics and gesture motion. API success during
  the combination run does not by itself verify the physical wheel, servo,
  display, or speaker behavior; that requires the user's direct observation.
- **Still unverified for Stage B:** long-duration reset and heap stability plus
  servo temperature over the planned two-hour run.
- **User-observed for Stage B:** after the Stage B firmware was flashed, the
  user confirmed the physical behavior was satisfactory, covering the visible
  Idle expressions, small gimbal gestures and return, manual interruption, and
  lack of motor motion during the observed Idle period.
- **Explicitly waived:** the user chose to skip the planned two-hour stability
  and servo-temperature validation. Long-duration reset, heap, and thermal
  behavior therefore remains unverified and is not claimed as passed.
