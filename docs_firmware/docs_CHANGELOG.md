# SwimTrack Firmware — Changelog

All notable changes to the SwimTrack firmware are documented here.

---

## v1.0.0-dev (current)

**Released:** March 2026  
**Tested on:** ESP32 30-pin dev board + MPU-6050 (GY-521)  
**Final target:** ESP32-S2 WEMOS S2 Mini + MPU-6500 (GY-6500)  
**Framework:** PlatformIO + Arduino, C++17

### Features

#### Prompt 1 — IMU Driver
- Implemented `MPU6500` driver class targeting MPU-6050/6500 family
- I2C initialisation at 400 kHz with WHO_AM_I verification
- Sensor configuration: ±8g accelerometer, ±1000 dps gyroscope, DLPF_CFG=3
- 14-byte burst read from register 0x3B producing one `IMUSample` per call
- Conversion to physical units: g, dps, °C
- 50 Hz non-blocking sample loop using `millis()` with drift guard
- Fatal error LED blink pattern on init failure

#### Prompt 2 — IMU Filters
- `EMAFilter` class: single-pole IIR low-pass filter, alpha=0.3
- First-sample seeding prevents startup transient in Serial Plotter
- `accelMagnitude()` using `sqrtf()` (single-precision, avoids double promotion on Xtensa)
- `filteredMagnitude()` convenience wrapper
- Serial Plotter output: `raw_mag filtered_mag` (2 labelled traces)

#### Prompt 3 — Stroke Detector
- Two-state FSM (BELOW / ABOVE) counts strokes on falling edge
- Dynamic baseline: slow EMA (alpha=0.02) tracking gravity, updated in BELOW state only
- Detection threshold = baseline + `STROKE_THRESHOLD_G` (0.4g)
- `STROKE_MIN_GAP_MS` (500ms) gap guard on rising edge prevents double-counting
- Circular buffer of last 5 inter-stroke intervals → strokes-per-minute
- `_classifyStroke()` stub always returns FREESTYLE (full classification deferred)
- Serial Plotter: `filtered_mag threshold above` (3 traces, `above` = 0/0.5 square wave)
- LED flashes 2ms on each confirmed stroke

#### Prompt 4 — Lap Counter + Rest Detection
- 3-state turn FSM: IDLE → SPIKE → GLIDE_WAIT → LAP CONFIRMED
- Spike must exceed 150 dps for at least 30ms (noise rejection)
- Glide phase requires accel < 1.2g for 100ms within a 2-second window
- `LAP_MIN_DURATION_MS` guard (15s pool, 5s bench testing)
- SWOLF = stroke_count + lap_duration_seconds
- Rest detection: rolling 1-second variance of filtered magnitude
- Variance < 0.05 g² sustained for 5s → resting state
- One-shot `restJustStarted()` / `restJustEnded()` pulse flags
- Serial Plotter: 5 traces (filtered_mag, threshold, gyroZ_norm, variance_x10, resting)
- LED flashes 200ms on lap confirmation

#### Prompt 5 — Session Manager + LittleFS Storage
- LittleFS mount with auto-format on first boot
- `/sessions/` directory auto-created
- Session lifecycle: `startSession()` → `recordLap()` / `recordRest()` → `stopSession()`
- `stopSession()` serialises full session to JSON, saves to `/sessions/<id>.json`
- `listSessions()` reads all files and prints formatted summary table
- `printSession(id)` streams full JSON to Serial
- `deleteSession(id)` removes file from flash
- `printFSInfo()` reports used/total flash space
- ArduinoJson v7 API used throughout: `JsonDocument`, `arr.add<JsonObject>()`, `doc["k"].to<JsonArray>()`

#### Prompt 6 — WiFi SoftAP + REST API
- ESP32 SoftAP: SSID=SwimTrack, pass=swim1234, IP=192.168.4.1
- `WebServer` on port 80, CORS headers on all responses
- Endpoints: GET /api/status, /api/live, /api/sessions, /api/sessions/\<id\>
- POST /api/session/start, /api/session/stop, /api/config
- DELETE /api/sessions/\<id\>
- `/api/live` provides real-time IMU values + stroke/lap metrics
- Free-function interface (`wifiBegin` / `wifiLoop`) avoids name collision with Arduino `WiFiServer`
- Shared state accessed via accessor functions across split files

#### Prompt 7 — Web Dashboard
- Single-page HTML dashboard served at `GET /`
- Live tile display: strokes, laps, SWOLF estimate, stroke rate, resting indicator
- Auto-refresh polls `/api/live` every second using `fetch()`
- Start / Stop session buttons calling POST endpoints
- Session history list loaded from `/api/sessions`
- Responsive design, works on phone browser

#### Prompt 8 — Power Manager (skipped)
- Battery ADC, deep sleep, and TP4056 charger integration deferred to hardware v2
- Stub battery values (100%, 4.20V) served from `/api/status`
- Will be implemented when soldering final ESP32-S2 + LiPo hardware

#### Prompt 9 — Full Integration + State Machine
- Device state machine: IDLE | RECORDING | SLEEPING
- Startup self-test: WHO_AM_I check + LittleFS mount + firmware version print
- Boot LED pattern: 2 slow blinks = pass, rapid blink = fail + halt
- BOOT button (GPIO 0): short press = toggle IDLE/RECORDING, long press (3s) = full reset
- Auto deep sleep: enters `esp_deep_sleep_start()` after 5 minutes idle with no WiFi client
- Wake on button: `ESP_SLEEP_WAKEUP_EXT0` on GPIO 0
- RECORDING state: full 50Hz pipeline active (strokes, laps, session accumulation)
- IDLE state: IMU still running at 50Hz for `/api/live` accuracy, algorithms paused
- Heap monitoring: free heap reported in every `[RATE]` and `[IMU]` debug line

### Known Issues / Limitations

- `_classifyStroke()` always returns FREESTYLE — gyro-based style classification not yet implemented
- Battery ADC not implemented (Prompt 8 skipped) — stub values returned
- Session ID uses `millis()` (ms since boot) instead of a real-time clock timestamp
- `LAP_MIN_DURATION_MS` must be manually changed between bench testing (5000) and pool use (15000)
- Sporadic I2C Error 263 on loose wiring — benign, auto-recovers next sample

### Migration Notes (dev → final hardware)

See `docs/README.md` Section 11 for the exact 5-line diff to migrate from ESP32+MPU-6050 to ESP32-S2+MPU-6500.

---

## v0.x — Development Milestones (not released)

| Milestone | Description |
|-----------|-------------|
| v0.1 | Basic IMU read + Serial print at 50 Hz |
| v0.2 | EMA filter + Serial Plotter traces |
| v0.3 | Stroke detection FSM + LED flash |
| v0.4 | Lap counter + rest detection |
| v0.5 | Session JSON → LittleFS |
| v0.6 | WiFi SoftAP + REST endpoints |
| v0.7 | Web dashboard |
| v0.9 | State machine + self-test + button + sleep |
| v1.0-dev | Full integration, all prompts complete except battery |
