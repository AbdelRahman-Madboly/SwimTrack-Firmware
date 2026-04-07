# SwimTrack Firmware — Changelog

All notable changes to the SwimTrack ESP32 firmware are documented here.  
Format: [Semantic Versioning](https://semver.org) · Dates: YYYY-MM

---

## [1.1.0] — 2026-04 — Bug Fixes & App Integration

### Fixed
- `main.dart` simulator mode sync: `DeviceApiService` now pre-loads `simulatorMode`
  from `SharedPreferences` on startup — prevents the singleton from making real
  HTTP calls on the first frame when simulator mode was ON from a previous session
- `settings_tab.dart` layout overflow: `ConnectionStatusWidget` wrapped in
  `Expanded` to prevent 24 px overflow on narrow displays (e.g. Samsung A54)
- `ref.listen<AppSettings>` in `main.dart` now uses explicit generic type and
  typed callback parameters — fixes all `undefined_identifier` and
  `unchecked_use_of_nullable_value` Dart errors

### Changed
- App icon updated to custom SwimTrack branding (swimmer + wave, 1024×1024 PNG)
- `flutter_launcher_icons` configuration updated with `remove_alpha_ios: true`
  to eliminate App Store alpha-channel warning

### Notes
- No firmware changes in this release — all fixes are app-side only
- Firmware v1.0.0 tested and confirmed working on real hardware (LOLIN S2 Mini +
  MPU-6500) alongside the updated app

---

## [1.0.0] — 2026-03 — First Complete Release

### Hardware
- Target: LOLIN S2 Mini (ESP32-S2) + GY-6500 (MPU-6500)
- Dev board used for all testing: ESP32 30-pin + GY-521 (MPU-6050)
- I2C: SCL → GPIO 33, SDA → GPIO 34 (dev: GPIO 22 / 21)
- LED → GPIO 15 (dev: GPIO 2)
- WHO_AM_I: 0x70 for MPU-6500 (dev: 0x68 for MPU-6050)

### Added — IMU Driver (Prompt 1)
- Full MPU-6500 / MPU-6050 driver over I2C at 400 kHz
- 14-byte burst read: accelerometer (X/Y/Z), temperature, gyroscope (X/Y/Z)
- Unit conversion: raw → g (accel) and dps (gyro) using configurable sensitivity
- Hardware self-test: WHO_AM_I register check on boot
- Confirmed output: `WHO_AM_I=0x68`, stable 50.00 Hz on dev hardware

### Added — IMU Filters (Prompt 2)
- Exponential Moving Average (EMA) filter, α = 0.3, applied to all 6 axes
- Accelerometer magnitude computed from filtered X/Y/Z for downstream detectors
- Serial Plotter verified: clean separation of raw vs filtered traces

### Added — Stroke Detector (Prompt 3)
- Two-state FSM (IDLE / ABOVE_THRESHOLD) on EMA-filtered accel magnitude
- Dynamic baseline: rolling average of quiet-phase magnitude
- Stroke counted on falling edge back to IDLE
- Minimum gap enforcement: `STROKE_MIN_GAP_MS` (500 ms) prevents double-counting
- Stroke rate: exponential moving average of inter-stroke intervals → spm
- LED 2 ms flash on each confirmed stroke
- Confirmed output: manual count matches ±1 over 10 strokes

### Added — Lap Counter + Rest Detection (Prompt 4)
- Turn detection FSM on gyro-Z axis: spike > `TURN_GYRO_Z_THRESH_DPS` (150 °/s)
  followed by glide period > `TURN_GLIDE_MIN_MS` with low accel magnitude
- Minimum lap duration guard: `LAP_MIN_DURATION_MS` (5 s bench / 15 s pool)
- Per-lap data: lap number, time (s), stroke count, SWOLF, stroke rate (spm),
  distance per stroke (DPS, m/stroke)
- Rest detection: rolling variance of accel magnitude over 2 s window;
  variance < `REST_VARIANCE_THRESH` (0.05 g²) for `REST_DURATION_MS` (5 s) → rest declared
- LED 200 ms flash on each confirmed lap
- Confirmed output: turn + rest correctly detected in bench testing

### Added — Session Manager + LittleFS (Prompt 5)
- LittleFS filesystem mounted on 4 MB flash partition
- Sessions stored as JSON files under `/sessions/<id>.json`
- Per-session data: ID (millis at start), start/end ms, pool length, lap array,
  rest array, total strokes, avg SWOLF, avg SPM, avg DPS, total distance
- CRUD: save, list, read by ID, delete by ID
- Maximum: 80 laps and 20 rests per session; 20 KB JSON capacity per session
- Serial commands: `s` start · `x` stop · `l` list · `p` print · `d` delete ·
  `r` reset · `f` flash info · `i` live stats
- Confirmed: `/sessions/12345.json` (487 bytes) saved/listed/printed/deleted

### Added — WiFi SoftAP + REST API (Prompt 6)
- WiFi Access Point: SSID `SwimTrack`, password `swim1234`, IP `192.168.4.1`
- HTTP server on port 80 using `WebServer` (Arduino ESP32 core)
- REST endpoints:

  | Method | Path | Description |
  |--------|------|-------------|
  | GET | `/api/status` | Mode, battery stub, uptime, heap, pool length |
  | GET | `/api/live` | Real-time IMU + stroke/lap metrics at current moment |
  | GET | `/api/sessions` | Summary list of all saved sessions |
  | GET | `/api/sessions/<id>` | Full session JSON with lap_data + rests arrays |
  | POST | `/api/session/start` | Body: `{"pool_length_m":25}` → starts recording |
  | POST | `/api/session/stop` | Saves active session, returns `saved_id` |
  | POST | `/api/config` | Update pool length without starting a session |
  | DELETE | `/api/sessions/<id>` | Delete session file from flash |

- CORS headers on all responses (`Access-Control-Allow-Origin: *`)
- Float values serialised as strings via `serialized(String(value, N))` in ArduinoJson v7
  (matches `LiveData.fromJson()` parsing in the Flutter app)
- Confirmed: all endpoints respond correctly, `clients=1` verified on phone

### Added — Web Dashboard (Prompt 7)
- Single-page HTML dashboard served from PROGMEM at `GET /`
- Live tiles: strokes, rate (spm), laps, SWOLF, resting indicator — auto-refresh 1 Hz
- Start / Stop session buttons wired to POST endpoints
- Session history list loaded from `GET /api/sessions`
- Works on phone browser; responsive layout for mobile screens

### Added — Full Integration + State Machine (Prompt 9)
- Device state machine with three states:
  - `IDLE` — IMU running at 50 Hz, algorithms paused, API available
  - `RECORDING` — full pipeline active (strokes, laps, session accumulation)
  - `SLEEPING` — `esp_deep_sleep_start()` after 5 min idle with no WiFi client
- Startup self-test sequence:
  - WHO_AM_I register check (IMU)
  - LittleFS mount check
  - LED pattern: 2 slow blinks = pass · rapid blink = fatal error + halt
  - Firmware version printed: `[SELF-TEST] SwimTrack FW v1.0.0`
- BOOT button (GPIO 0): short press = toggle IDLE ↔ RECORDING ·
  long press 3 s = full counter + filesystem reset
- Wake from sleep: `ESP_SLEEP_WAKEUP_EXT0` on GPIO 0
- Heap monitoring: free heap reported in every `[RATE]` line
- 50 Hz main loop stable under full pipeline load
- Confirmed output:
  ```
  [SELF-TEST] ALL PASS ✓
  [STATE] → RECORDING (pool=25m)
  [LAP] === LAP #3 CONFIRMED === Time:6.7s Strokes:4 SWOLF:10.7
  [SESSION] Saved: /sessions/12010.json (388 bytes)
  [RATE] 50.0Hz | state=RECORDING | clients=1 | heap=236088
  ```

### Skipped — Power Manager (Prompt 8)
- Battery ADC, deep sleep via TP4056, and LiPo integration deferred to
  final soldered hardware (ESP32-S2 + LiPo v2)
- `/api/status` returns stub values: `battery_pct: 100`, `battery_v: 4.20`

### Known Limitations
- `_classifyStroke()` always returns `FREESTYLE` — gyro-based stroke type
  classification not yet implemented
- Session ID is `millis()` since boot — not a Unix timestamp; no RTC or NTP
- `LAP_MIN_DURATION_MS` must be manually changed between bench (5000 ms)
  and pool (15 000 ms) use
- Sporadic I2C Error 263 on loose wiring — benign, auto-recovers next sample
- Battery ADC not implemented (stub values only)

---

## [0.x] — 2026-02/03 — Development Milestones (not released)

| Milestone | Description |
|-----------|-------------|
| v0.1 | Basic MPU-6050 I2C read + Serial print at 50 Hz |
| v0.2 | EMA filter + Serial Plotter traces |
| v0.3 | Stroke detection FSM + LED flash on stroke |
| v0.4 | Lap counter FSM + rest detection |
| v0.5 | Session JSON serialisation → LittleFS |
| v0.6 | WiFi SoftAP + all REST endpoints |
| v0.7 | Web dashboard (HTML from PROGMEM) |
| v0.9 | State machine + self-test + button handler + deep sleep |
| v1.0-dev | Full integration · all modules complete except battery ADC |

### Issues Resolved During Development

| Issue | Module | Resolution |
|-------|--------|------------|
| `stroke_detector_part2.cpp` missing from `src/` | P3 | File added |
| `ArduinoJson.h` not found by linker | P5 | Added to `platformio.ini` `lib_deps` |
| Part2 session manager file truncated at line 164 | P5 | File rewritten in full |
| ArduinoJson v6 API (`StaticJsonDocument`) deprecated in v7 | P5, P6 | Migrated to `JsonDocument` throughout |
| `WiFi was not declared in this scope` | P6 | Added `#include <WiFi.h>` to `wifi_server.cpp` |
| WiFi handler files truncated mid-function | P6 | All three files (`wifi_server`, `wifi_live`, `wifi_api`) rewritten |
| `main_p9_loop.cpp` left in `src/` causing duplicate `loop()` symbol | P9 | File deleted, logic merged into `main.cpp` |
| `containsKey()` deprecated in ArduinoJson v7 | P9 | Replaced with `.isNull()` check |
| Arduino IDE "multiple definition of setup/loop" | All | Remove `main.cpp` from `.ino` folder if using Arduino IDE |

---

## Roadmap

- [ ] Battery ADC — read LiPo voltage via GPIO 1 divider, replace stub values
- [ ] Stroke type classification — gyro-based FREESTYLE / BACKSTROKE / BREASTSTROKE / BUTTERFLY
- [ ] RTC or NTP timestamp — real Unix timestamps for session `start_ms` / `end_ms`
- [ ] OTA firmware update — via `/api/ota` endpoint over WiFi
- [ ] Pool-auto-detect — infer pool length from GPS or session duration heuristic

---

*Firmware repo: https://github.com/AbdelRahman-Madboly/SwimTrack-Firmware.git*