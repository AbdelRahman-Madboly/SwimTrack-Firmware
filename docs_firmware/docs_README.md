# SwimTrack Firmware Documentation

**Version:** 1.0.0-dev  
**Target hardware:** ESP32-S2 WEMOS S2 Mini + MPU-6500 (GY-6500)  
**Dev/test hardware:** ESP32 30-pin dev board + MPU-6050 (GY-521)  
**Framework:** PlatformIO + Arduino (C++17)

---

## 1. Project Overview

SwimTrack is a wrist-worn embedded swim training computer. It detects strokes, counts laps, calculates SWOLF efficiency scores, stores sessions to flash, and streams live data and stored sessions to a phone via a WiFi REST API.

### Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        SwimTrack Firmware                        │
│                                                                   │
│  ┌──────────┐   raw     ┌────────────┐  filtMag  ┌───────────┐  │
│  │ MPU-6050 │──samples──▶ imu_filters │──────────▶│  stroke_  │  │
│  │ /6500    │  (50 Hz)  │  (EMA 0.3) │           │ detector  │  │
│  └──────────┘           └────────────┘           └─────┬─────┘  │
│       │                       │                        │stroke   │
│       │ gz (raw)              │ filtMag                │count    │
│       │                       ▼                        ▼         │
│       │              ┌──────────────┐      ┌──────────────────┐  │
│       └──────────────▶ lap_counter  │      │ session_manager  │  │
│                      │ turn FSM     │─lap──▶ (LittleFS JSON)  │  │
│                      │ rest detect  │─rest─▶                  │  │
│                      └──────────────┘      └────────┬─────────┘  │
│                                                     │             │
│                      ┌──────────────────────────────▼──────────┐ │
│                      │            wifi_server                    │ │
│                      │  GET /api/live   GET /api/sessions        │ │
│                      │  POST /api/session/start|stop             │ │
│                      │  GET / (web dashboard)                    │ │
│                      └──────────────────────────────────────────┘ │
│                                                                   │
│  ┌─────────────────────────────────────────────────────────────┐ │
│  │                    main.cpp — State Machine                   │ │
│  │   IDLE ──(btn/API)──▶ RECORDING ──(btn/API)──▶ IDLE         │ │
│  │   IDLE ──(5 min no client)──▶ SLEEPING ──(btn)──▶ IDLE      │ │
│  └─────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘

Phone Browser ◀──── WiFi SoftAP 192.168.4.1 ────▶ REST API + Dashboard
```

### Module Responsibilities

| Module | Files | Responsibility |
|--------|-------|---------------|
| IMU Driver | `mpu6500.h/.cpp/_part2` | I2C init, 14-byte burst read, unit conversion |
| IMU Filters | `imu_filters.h/.cpp` | EMA filter, accel magnitude computation |
| Stroke Detector | `stroke_detector.h/.cpp/_part2` | Two-state FSM, dynamic baseline, stroke rate |
| Lap Counter | `lap_counter.h/.cpp/_part2` | Turn FSM, rest detection, variance window |
| Session Manager | `session_manager.h/.cpp/_part2/_part3` | LittleFS, JSON serialisation, CRUD |
| WiFi Server | `wifi_server.h/.cpp`, `wifi_live.cpp`, `wifi_api.cpp` | SoftAP, REST API, dashboard |
| Main | `main.cpp` | State machine, self-test, button, sample loop |

---

## 2. Hardware

### Dev / Testing Hardware (current)

| Component | Part | Notes |
|-----------|------|-------|
| MCU | ESP32 30-pin dev board (CP2102 USB) | Available on Amazon/AliExpress |
| IMU | MPU-6050 GY-521 breakout | AD0 floating → address 0x68 |
| I2C SCL | GPIO **22** | 4.7kΩ pull-up to 3.3V recommended |
| I2C SDA | GPIO **21** | 4.7kΩ pull-up to 3.3V recommended |
| LED | GPIO **2** | Built-in on dev board |
| Button | GPIO **0** | BOOT button (built-in, active LOW) |
| Power | USB 5V via CP2102 | No battery for dev testing |

### Final / Production Hardware (target)

| Component | Part | Notes |
|-----------|------|-------|
| MCU | ESP32-S2 WEMOS S2 Mini | Compact, single-core, native USB |
| IMU | MPU-6500 GY-6500 breakout | AD0 tied to GND → address 0x68 |
| I2C SCL | GPIO **33** | 4.7kΩ pull-up to 3.3V |
| I2C SDA | GPIO **34** | 4.7kΩ pull-up to 3.3V |
| LED | GPIO **15** | Built-in on WEMOS S2 Mini |
| Button | GPIO **0** | BOOT button (built-in, active LOW) |
| Battery | 3.7V LiPo via TP4056 | ~500mAh recommended |
| Battery ADC | GPIO **1** | Via 100k/100k voltage divider |

### Dev vs Final — Side-by-Side Differences

| # | Property | Dev Kit | Final Hardware |
|---|----------|---------|----------------|
| 1 | **MCU** | ESP32 (dual-core, 240 MHz) | ESP32-S2 (single-core, 240 MHz, native USB) |
| 2 | **IMU** | MPU-6050 | MPU-6500 |
| 3 | **WHO_AM_I** | `0x68` | `0x70` |
| 4 | **Temp formula** | `raw / 340.0 + 36.53` | `raw / 333.87 + 21.0` |
| 5 | **I2C pins** | SCL=22, SDA=21 | SCL=33, SDA=34 |

### Wiring Diagram — Dev Kit

```
ESP32 Dev Board          GY-521 (MPU-6050)
─────────────────        ─────────────────
3.3V  ────────────────▶  VCC
GND   ────────────────▶  GND
GPIO22 ───────────────▶  SCL
GPIO21 ───────────────▶  SDA
                         AD0  (leave floating — board has pull-down)
```

### Wiring Diagram — Final Hardware

```
WEMOS S2 Mini            GY-6500 (MPU-6500)      TP4056 Charger
─────────────────        ──────────────────      ──────────────
3.3V  ────────────────▶  VCC                     OUT+ ──▶ 5V (USB)
GND   ────────────────▶  GND                     B+/B- ──▶ LiPo
GPIO33 ───────────────▶  SCL                     OUT+ ──▶ 3.3V reg ──▶ ESP32 3V3
GPIO34 ───────────────▶  SDA
GND   ────────────────▶  AD0 (tie LOW → 0x68)
GPIO1  ◀──────────────── Battery voltage via 100kΩ/100kΩ divider
```

---

## 3. Build Instructions

### Prerequisites

1. Install [VS Code](https://code.visualstudio.com/)
2. Install the [PlatformIO IDE extension](https://platformio.org/install/ide?install=vscode)
3. Clone or copy the SwimTrack firmware folder

### platformio.ini

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
lib_deps =
    bblanchon/ArduinoJson@^7.0.0
board_build.filesystem = littlefs
monitor_speed = 115200
```

For the final ESP32-S2 target, change:
```ini
[env:esp32s2]
platform = espressif32
board = wemos_s2_mini
framework = arduino
lib_deps =
    bblanchon/ArduinoJson@^7.0.0
board_build.filesystem = littlefs
monitor_speed = 115200
```

### Library Install

ArduinoJson v7 is declared in `platformio.ini` and downloaded automatically on first build. All other dependencies (Wire, LittleFS, WiFi, WebServer) are built into the ESP32 Arduino framework.

### Build & Flash

```bash
# First-time: upload the filesystem image (creates LittleFS partition)
pio run --target uploadfs

# Upload firmware
pio run --target upload

# Open serial monitor
pio device monitor
```

---

## 4. First Upload

### Dev Kit (ESP32 30-pin + CP2102)

1. Install the [CP2102 USB driver](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers)
2. Connect the board via USB
3. In Device Manager (Windows), confirm a COM port appears under "Ports (COM & LPT)"
4. In `platformio.ini`, the board will be detected automatically
5. Run `pio run --target upload` — the ESP32 enters flash mode automatically via the CP2102 DTR/RTS lines

### Final Hardware (ESP32-S2 WEMOS S2 Mini)

The ESP32-S2 uses native USB and requires manual boot mode entry for the first flash:

1. Hold the **BOOT button** (GPIO 0)
2. While holding BOOT, press and release **RESET**
3. Release BOOT — the board is now in download mode
4. Run `pio run --target upload`
5. After upload completes, press **RESET** to start the firmware

> **Note:** After the first successful upload via PlatformIO, subsequent uploads may work automatically depending on the ESP-IDF version. If upload fails, repeat the BOOT+RESET procedure.

---

## 5. config.h Reference

All tunable constants live in `include/config.h`. Never hard-code these values in algorithm files.

### Debug

| Define | Default | Description |
|--------|---------|-------------|
| `DEBUG_SERIAL` | `1` | Set to `0` to strip all `[TAG]` debug output from the binary |

### Pin Assignments (Final Hardware)

| Define | Default | Description |
|--------|---------|-------------|
| `PIN_I2C_SCL` | `33` | I2C clock — change to `22` for dev kit |
| `PIN_I2C_SDA` | `34` | I2C data — change to `21` for dev kit |
| `PIN_LED` | `15` | Built-in LED — change to `2` for dev kit |
| `PIN_BATTERY_ADC` | `1` | ADC pin for battery voltage divider |
| `PIN_BUTTON` | `0` | BOOT button, active LOW, internal pull-up |

### IMU Settings

| Define | Default | Description |
|--------|---------|-------------|
| `MPU6500_I2C_ADDR` | `0x68` | I2C address (AD0 tied LOW) |
| `MPU_WHO_AM_I_VAL` | `0x70` | Expected WHO_AM_I (MPU-6500). Use `0x68` for MPU-6050 |
| `I2C_CLOCK_HZ` | `400000` | I2C bus speed (400 kHz fast-mode) |
| `SAMPLE_RATE_HZ` | `50` | IMU sample rate in Hz |
| `SAMPLE_PERIOD_MS` | `20` | Derived: 1000 / SAMPLE_RATE_HZ |
| `ACCEL_FS_SEL` | `2` | Accel range: 0=±2g, 1=±4g, **2=±8g**, 3=±16g |
| `GYRO_FS_SEL` | `2` | Gyro range: 0=±250, 1=±500, **2=±1000**, 3=±2000 dps |
| `ACCEL_SENSITIVITY` | `4096.0` | LSB/g at ±8g (from datasheet Table 1) |
| `GYRO_SENSITIVITY` | `32.8` | LSB/dps at ±1000 dps (from datasheet Table 2) |

### Stroke Detection

| Define | Default | Tuning Guidance |
|--------|---------|-----------------|
| `EMA_ALPHA` | `0.3` | Filter smoothness. Lower = smoother but more lag. Range: 0.1–0.5 |
| `STROKE_THRESHOLD_G` | `0.4` | g above baseline to trigger stroke. Raise if false positives; lower if misses |
| `STROKE_MIN_GAP_MS` | `500` | Min ms between strokes (= max 120 spm). Raise to 600–700 if double-counting |

### Turn / Lap Detection

| Define | Default | Tuning Guidance |
|--------|---------|-----------------|
| `TURN_GYRO_Z_THRESH_DPS` | `150.0` | Gyro Z spike to start turn detection. Raise if noise triggers; lower if turns miss |
| `TURN_GLIDE_WINDOW_MS` | `2000` | Window after spike to see glide. Increase for slower swimmers |
| `GLIDE_ACCEL_THRESH_G` | `1.2` | Max accel during glide confirmation. Lower if glide not detected |
| `LAP_MIN_DURATION_MS` | `15000` | **Set to 5000 for bench testing, 15000 for pool use** |

### Rest Detection

| Define | Default | Tuning Guidance |
|--------|---------|-----------------|
| `REST_VARIANCE_THRESH` | `0.05` | g² variance below = resting. Raise if rest triggers during slow swimming |
| `REST_DURATION_MS` | `5000` | ms of low variance before entering rest state |

### Pool & Session

| Define | Default | Description |
|--------|---------|-------------|
| `DEFAULT_POOL_LENGTH_M` | `25` | Pool length in metres (overridable via API) |
| `SESSION_DIR` | `"/sessions"` | LittleFS directory for session JSON files |
| `SESSION_MAX_LAPS` | `80` | Max laps per session (covers 2000m in 25m pool) |
| `SESSION_MAX_RESTS` | `20` | Max rest intervals per session |
| `SESSION_JSON_CAPACITY` | `20480` | ArduinoJson heap allocation for serialisation |

### WiFi

| Define | Default | Description |
|--------|---------|-------------|
| `WIFI_AP_SSID` | `"SwimTrack"` | SoftAP network name |
| `WIFI_AP_PASS` | `"swim1234"` | WPA2 password (min 8 chars) |
| `WIFI_AP_IP` | `"192.168.4.1"` | Device IP address |

### Battery / Power

| Define | Default | Description |
|--------|---------|-------------|
| `BATT_DIVIDER_RATIO` | `0.5` | R2/(R1+R2) — 0.5 for 100k/100k divider |
| `ADC_VREF_MV` | `3300` | ESP32-S2 ADC reference voltage (mV) |
| `ADC_MAX_COUNT` | `4095` | 12-bit ADC maximum |
| `BATT_FULL_MV` | `4200` | LiPo full charge voltage (mV) |
| `BATT_EMPTY_MV` | `3300` | LiPo cut-off voltage (mV) |
| `BATT_SLEEP_MV` | `3400` | Trigger deep sleep below this voltage (mV) |

---

## 6. REST API Reference

Base URL: `http://192.168.4.1`  
All responses include `Content-Type: application/json` and `Access-Control-Allow-Origin: *`.

### GET /

Returns the HTML web dashboard page.

---

### GET /api/status

**Response:**
```json
{
  "mode": "IDLE",
  "session_active": false,
  "wifi_clients": 1,
  "uptime_s": 120,
  "battery_pct": 85,
  "battery_v": 3.95,
  "pool_m": 25,
  "free_heap": 242108
}
```
`mode` is `"IDLE"` or `"RECORDING"`.

---

### GET /api/live

Poll at 1–2 Hz during a session for real-time metrics.

**Response:**
```json
{
  "ax": 0.012, "ay": -0.098, "az": 0.897,
  "gx": 2.35,  "gy": 1.55,  "gz": 0.21,
  "temp_c": 27.3,
  "strokes": 14,
  "lap_strokes": 3,
  "rate_spm": 38.5,
  "stroke_type": "FREESTYLE",
  "laps": 2,
  "resting": false,
  "lap_elapsed_s": 18.4,
  "variance": 0.0124,
  "swolf_est": 21.4,
  "session_active": true,
  "session_laps": 2
}
```

---

### GET /api/sessions

Returns array of session summaries (no per-lap data).

**Response:**
```json
[
  {
    "id": 12010,
    "duration_s": 22.7,
    "laps": 4,
    "total_strokes": 8,
    "pool_m": 25,
    "total_dist_m": 100,
    "avg_swolf": 9.7
  }
]
```

---

### GET /api/sessions/\<id\>

Returns the full session JSON from LittleFS.

**Response:**
```json
{
  "id": 12010,
  "start_ms": 12010,
  "end_ms": 34737,
  "duration_s": 22.7,
  "pool_m": 25,
  "laps": 4,
  "total_strokes": 8,
  "total_dist_m": 100,
  "avg_swolf": 9.7,
  "avg_spm": 38.5,
  "lap_data": [
    { "n": 1, "t_s": 5.3, "strokes": 2, "swolf": 7.3, "spm": 38.5 }
  ],
  "rests": [
    { "start_ms": 45000, "dur_s": 12.3 }
  ]
}
```

---

### POST /api/session/start

**Request body (optional):**
```json
{ "pool_length_m": 25 }
```

**Response:**
```json
{ "ok": true, "pool_m": 25 }
```

---

### POST /api/session/stop

No request body.

**Response:**
```json
{ "ok": true, "saved_id": 12010 }
```

---

### POST /api/config

**Request body:**
```json
{ "pool_length_m": 50 }
```

**Response:**
```json
{ "ok": true, "pool_m": 50 }
```

---

### DELETE /api/sessions/\<id\>

**Response (success):** `{ "ok": true }`  
**Response (not found):** `{ "error": "session not found" }`

---

## 7. Algorithm Explanations

### Stroke Detector — FSM

```
                    filtMag > baseline + STROKE_THRESHOLD_G
                    AND time since last stroke >= STROKE_MIN_GAP_MS
         ┌──────┐  ─────────────────────────────────────────────▶  ┌───────┐
         │      │                                                    │       │
  start  │BELOW │         (baseline EMA updated here)               │ ABOVE │
  ──────▶│      │  ◀─────────────────────────────────────────────  │       │
         └──────┘  filtMag <= threshold                             └───────┘
                                                                    STROKE COUNTED
                                                                    on falling edge
```

The **dynamic baseline** is a very slow EMA (alpha=0.02) that tracks gravity level. It only updates in the BELOW state, so arm-swing peaks cannot inflate it. The detection threshold = baseline + 0.4g floats with the swimmer's resting orientation.

---

### Turn Detection — FSM

```
          |gyroZ| > 150dps                spike < 30ms
          AND lap time ok                (noise reject)
  ┌──────┐ ──────────────▶ ┌───────┐ ──────────────────▶ ┌──────┐
  │      │                  │       │                       │      │
  │ IDLE │                  │ SPIKE │                       │ IDLE │
  │      │ ◀──────────────  │       │
  └──────┘  window timeout  └───┬───┘
       ▲    (2000ms)            │ spike ends,
       │                        │ dur >= 30ms
       │  window timeout        ▼
       │  (no glide seen)  ┌────────────┐
       └───────────────────│ GLIDE_WAIT │
                           │            │
                           └─────┬──────┘
                                 │ filtMag < 1.2g
                                 │ for >= 100ms
                                 ▼
                            LAP CONFIRMED
                          ─────────────────
                          lapCount++
                          build LapRecord
                          reset lapStartMs
```

---

### Rest Detection

```
filtMag variance (1s window):

  variance < 0.05 g²                    variance >= 0.05 g²
  continuously for 5000ms               (any sample)
  ┌──────────┐ ──────────────────────▶ ┌─────────┐
  │          │                          │         │
  │  ACTIVE  │                          │ RESTING │
  │          │ ◀────────────────────── │         │
  └──────────┘  immediately             └─────────┘
```

Variance is computed from a circular buffer of the last 50 samples (1 second at 50 Hz) using the standard two-pass formula. The window recomputes from scratch each sample to avoid Welford numerical drift.

---

### SWOLF Formula

SWOLF (SWim gOLF) = **stroke count + lap time in seconds**

Lower is more efficient. It combines speed (time) with technique (strokes).

**Example:**
- Lap time: 30.0 seconds
- Strokes taken: 18
- SWOLF = 18 + 30.0 = **48.0**

**Improving SWOLF:**
- Fewer strokes (better technique/distance per stroke) → lower score
- Faster time (better fitness/speed) → lower score
- Elite swimmers achieve SWOLF < 35 in a 25m pool

---

## 8. Serial Commands Reference

Open Serial Monitor at **115200 baud**, **Newline** line ending.

| Key | State | Action |
|-----|-------|--------|
| `s` | Any | Start session — resets all counters, enters RECORDING |
| `x` | RECORDING | Stop session — saves JSON to LittleFS, returns to IDLE |
| `l` | Any | List all saved sessions (formatted table) |
| `p` | Any | Print full JSON of the last saved session |
| `d` | Any | Delete the last saved session from flash |
| `r` | Any | Reset stroke + lap counters only (session stays open if active) |
| `f` | Any | Print LittleFS used / total space |
| `i` | Any | Print live stats: state, laps, strokes, rate, resting, variance, elapsed, clients |

**Button (GPIO 0 — BOOT button):**

| Press | Action |
|-------|--------|
| Short press (<3s) | IDLE → RECORDING, or RECORDING → IDLE |
| Long press (≥3s) | Full reset: stop session, clear all counters, return to IDLE |

---

## 9. Troubleshooting Guide

### WHO_AM_I = 0x00

**Symptom:** `[IMU] ERROR: WHO_AM_I mismatch` with value `0x00`  
**Cause:** I2C communication failed completely — sensor not responding  
**Fix:**
1. Check SDA and SCL wires are not swapped
2. Verify VCC is 3.3V (not 5V)
3. Confirm GND is connected
4. Check for short circuits on the breadboard

---

### WHO_AM_I = 0x70 when expecting 0x68 (or vice versa)

**Symptom:** `[IMU] ERROR: WHO_AM_I mismatch — wrong chip or I2C address?`  
**Cause:** Running MPU-6500 code on MPU-6050 hardware, or vice versa  
**Fix:** In `config.h`, change `MPU_WHO_AM_I_VAL`:
- MPU-6050 → `0x68`
- MPU-6500 → `0x70`

Also update the temperature formula in `mpu6500.cpp`:
- MPU-6050: `raw / 340.0f + 36.53f`
- MPU-6500: `raw / 333.87f + 21.0f`

---

### No strokes detected

**Symptom:** Waving board produces no `[STROKE]` events  
**Causes and fixes:**
1. `STROKE_THRESHOLD_G` too high → lower to `0.25` in `config.h`
2. Board orientation wrong → ensure the arm swing creates acceleration along an axis
3. EMA filter over-smoothing → raise `EMA_ALPHA` to `0.4`
4. Check Serial Plotter (`filtered_mag` trace should spike clearly during arm pulls)

---

### Double-counted strokes

**Symptom:** Two `[STROKE]` events per arm cycle  
**Cause:** `STROKE_MIN_GAP_MS` too short for the swimmer's pace  
**Fix:** Raise `STROKE_MIN_GAP_MS` to `600` or `700` in `config.h`

---

### Lap triggers too early / during arm motion

**Symptom:** `[LAP]` fires unexpectedly, not on wall turns  
**Cause:** Arm rotation during freestyle exceeds `TURN_GYRO_Z_THRESH_DPS`  
**Fix:**
1. Raise `TURN_GYRO_Z_THRESH_DPS` to `180` or `200`
2. Check device orientation on wrist — Z-axis should point perpendicular to the pool wall
3. Ensure `LAP_MIN_DURATION_MS` is `15000` (not `5000`) for pool use

---

### LittleFS mount failed

**Symptom:** `[SESSION] ERROR: LittleFS mount failed`  
**Cause:** Wrong partition scheme in PlatformIO  
**Fix:** Ensure `platformio.ini` contains:
```ini
board_build.filesystem = littlefs
```
Then run `pio run --target uploadfs` to format and upload the filesystem image.

---

### WiFi AP not visible on phone

**Symptom:** "SwimTrack" network does not appear in phone WiFi settings  
**Cause 1:** Board hasn't booted fully yet — wait 3 seconds after power-on  
**Cause 2:** `wifiBegin()` not called in `setup()` — check `main.cpp`  
**Cause 3:** Another device is using the same SSID — change `WIFI_AP_SSID` in `config.h`  
**Verification:** Serial Monitor should show `[WIFI] AP started. SSID=SwimTrack  IP=192.168.4.1`

---

### Battery reads 0V (ADC2 disabled during WiFi)

**Symptom:** Battery voltage always shows 0V or 0%  
**Cause:** On standard ESP32 (not S2), ADC2 pins are disabled when WiFi is active  
**Fix 1 (final hardware only):** ESP32-S2 does not have this limitation — use any ADC pin  
**Fix 2 (dev kit):** Use an ADC1 pin (GPIO 32–39) instead of ADC2 pins (GPIO 0, 2, 4, 12–15, 25–27)  
**Verification:** `analogReadMilliVolts(PIN_BATTERY_ADC)` should return a non-zero value

---

### I2C Error 263 (sporadic)

**Symptom:** `[Wire.cpp:499] requestFrom(): i2cWriteReadNonStop returned Error 263`  
**Cause:** Transient I2C bus error — usually a loose wire or signal integrity issue  
**Impact:** Single sample is dropped; firmware recovers automatically next cycle  
**Fix:** Shorten and secure I2C wires. Add 4.7kΩ pull-ups to SDA and SCL if not present.

---

## 10. Calibration Guide

### Stroke Detection Calibration

**Goal:** Every deliberate arm pull registers as exactly one stroke.

**Step 1 — Baseline check**  
Flash Prompt 3 firmware. Open Serial Plotter. Board flat on desk → `filtered_mag` should read ~0.90–1.00g, `threshold` should read ~1.30–1.40g. If threshold is much higher than 1.30g, the baseline is inflated — leave the board still for 30 seconds to let the slow EMA settle.

**Step 2 — Test with arm motion**  
Simulate freestyle strokes (wave the board). The `above` trace should jump to `0.5` on each arm pull and drop back to `0.0` between pulls. Count manually. If counts match: threshold is correct.

**Step 3 — Adjust `STROKE_THRESHOLD_G`**

| Symptom | Adjustment |
|---------|-----------|
| Missing strokes (arm pull doesn't register) | Decrease: `0.4` → `0.3` or `0.25` |
| False strokes at rest or during breathing | Increase: `0.4` → `0.5` or `0.6` |
| Double-counting one arm pull | Increase `STROKE_MIN_GAP_MS`: `500` → `600` |

**Step 4 — Validate in water**  
Count 10 strokes manually during a pool length. Compare with firmware count. Accept ±1 difference (one stroke may be cut off at the turn).

---

### Turn Detection Calibration

**Goal:** Every wall turn registers as exactly one lap. No false triggers from arm rotation.

**Step 1 — Simulate a turn (bench)**  
Rotate the board briskly around the Z-axis for ~300ms, then hold still. Expected output:
```
[LAP] Gyro spike detected: 180.0 dps
[LAP] Spike ended: peak=210.0 dps, dur=80ms
[LAP] Glide candidate: filtMag=0.95g
[LAP] === LAP #1 CONFIRMED ===
```

**Step 2 — Adjust `TURN_GYRO_Z_THRESH_DPS`**

| Symptom | Adjustment |
|---------|-----------|
| Lap triggers during freestyle arm rotation | Increase: `150` → `180` or `200` |
| Wall turns not detected (spike ignored) | Decrease: `150` → `120` |
| Glide not confirming (interrupted) | Lower `GLIDE_ACCEL_THRESH_G`: `1.2` → `1.3` |

**Step 3 — Set correct `LAP_MIN_DURATION_MS`**

| Environment | Value |
|-------------|-------|
| Bench testing | `5000` (5 seconds) |
| 25m pool | `15000` (15 seconds) |
| 50m pool | `25000` (25 seconds) |

---

## 11. Migration Guide

To move from the dev kit (ESP32 + MPU-6050) to the final hardware (ESP32-S2 + MPU-6500), make exactly these changes:

### config.h — 3 changes

```diff
- #define PIN_I2C_SCL       22
+ #define PIN_I2C_SCL       33

- #define PIN_I2C_SDA       21
+ #define PIN_I2C_SDA       34

- #define MPU_WHO_AM_I_VAL  0x68
+ #define MPU_WHO_AM_I_VAL  0x70
```

### mpu6500.cpp — 1 change (temperature formula in the `read()` function)

```diff
- sample.temp_c = static_cast<float>(rawTemp) / 340.0f + 36.53f;
+ sample.temp_c = static_cast<float>(rawTemp) / 333.87f + 21.0f;
```

### platformio.ini — 1 change

```diff
- board = esp32dev
+ board = wemos_s2_mini
```

Also update the LED pin if not already done (WEMOS S2 Mini LED is GPIO 15, not GPIO 2):
```diff
- #define PIN_LED   2
+ #define PIN_LED   15
```

**Everything else — all algorithm code, WiFi, LittleFS, session format, REST API — is identical between the two hardware revisions.**

---

## File Structure

```
SwimTrack/
├── docs/
│   ├── README.md          ← this file
│   └── CHANGELOG.md
├── include/
│   ├── config.h
│   ├── mpu6500.h
│   ├── imu_filters.h
│   ├── stroke_detector.h
│   ├── lap_counter.h
│   ├── session_manager.h
│   └── wifi_server.h
├── src/
│   ├── main.cpp
│   ├── mpu6500.cpp / _part2.cpp
│   ├── imu_filters.cpp
│   ├── stroke_detector.cpp / _part2.cpp
│   ├── lap_counter.cpp / _part2.cpp
│   ├── session_manager.cpp / _part2.cpp / _part3.cpp
│   ├── wifi_server.cpp
│   ├── wifi_live.cpp
│   └── wifi_api.cpp
└── platformio.ini
```
