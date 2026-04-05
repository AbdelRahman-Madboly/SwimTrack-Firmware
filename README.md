# SwimTrack

> ESP32-based wrist-worn swim training computer + Flutter companion app.

Detects swim strokes, counts laps, calculates SWOLF efficiency scores, stores sessions on flash memory, and syncs to a phone app over WiFi. Built as a complete hardware + software project: embedded C++ firmware on ESP32 and a Flutter Android app.

---

## Table of Contents

1. [What Is SwimTrack?](#1-what-is-swimtrack)
2. [Repository Structure](#2-repository-structure)
3. [Hardware](#3-hardware)
4. [Firmware](#4-firmware)
   - [Build & Flash](#41-build--flash)
   - [Firmware Architecture](#42-firmware-architecture)
   - [Build Status](#43-firmware-build-status)
   - [Serial Commands](#44-serial-commands)
   - [WiFi REST API](#45-wifi-rest-api)
   - [Algorithms](#46-algorithms)
   - [config.h Tuning Reference](#47-configh-tuning-reference)
   - [Troubleshooting](#48-firmware-troubleshooting)
   - [Migration to Final Hardware](#49-migration-to-final-hardware)
5. [Mobile App](#5-mobile-app)
   - [Tech Stack](#51-tech-stack)
   - [App Architecture](#52-app-architecture)
   - [Build & Run](#53-build--run)
   - [App Build Status](#54-app-build-status)
   - [Simulator Mode](#55-simulator-mode)
   - [Connecting to Device](#56-connecting-to-device)
   - [App Troubleshooting](#57-app-troubleshooting)
6. [API Reference](#6-api-reference)
7. [Changelog](#7-changelog)

---

## 1. What Is SwimTrack?

SwimTrack is a wrist-worn swim training computer that automatically tracks everything about a swim session — no manual lap-counting required. The device detects each stroke using an IMU accelerometer, identifies wall turns using the gyroscope, computes SWOLF scores (lower = better), and stores full session JSON to on-chip flash.

After swimming, connect your phone to the device WiFi network and the app pulls all session data, shows lap-by-lap breakdowns, SWOLF trend charts, and lets you start/stop sessions remotely with live metric monitoring.

**SWOLF** = stroke count + lap time in seconds. A score of 40 means you took 20 strokes in 20 seconds for one lap. Lower SWOLF means better efficiency.

---

## 2. Repository Structure

```
SwimTrack/
├── include/                    Firmware header files (.h)
│   ├── config.h                All tunable constants — edit here first
│   ├── mpu6500.h
│   ├── imu_filters.h
│   ├── stroke_detector.h
│   ├── lap_counter.h
│   ├── session_manager.h
│   └── wifi_server.h
│
├── src/                        Firmware source files (.cpp)
│   ├── main.cpp                State machine, self-test, button, 50 Hz sample loop
│   ├── mpu6500.cpp / _part2    I2C driver, burst read, unit conversion
│   ├── imu_filters.cpp         EMA filter, accel magnitude
│   ├── stroke_detector.cpp / _part2    Two-state FSM, dynamic baseline, stroke rate
│   ├── lap_counter.cpp / _part2       Turn FSM, rest detection, variance window
│   ├── session_manager.cpp / _part2 / _part3   LittleFS, JSON, CRUD
│   ├── wifi_server.cpp         WiFi AP setup, /api/status handler
│   ├── wifi_live.cpp           /api/live real-time handler
│   └── wifi_api.cpp            /api/sessions CRUD + start/stop handlers
│
├── Stages/                     Per-prompt isolated test mains (not compiled)
│   ├── main_p1_imu_test.cpp
│   ├── main_p2_imu_filters.cpp
│   └── ...
│
├── app/                        Flutter mobile app
│   ├── lib/
│   │   ├── main.dart
│   │   ├── config/             Theme, routes, constants
│   │   ├── models/             Session, Lap, LiveData, UserProfile, etc.
│   │   ├── providers/          Riverpod state (device, session, settings, live)
│   │   ├── services/           WiFi, HTTP, SQLite, sync
│   │   ├── screens/            Login, profile, home, history, detail, settings
│   │   └── widgets/            Reusable UI components
│   ├── android/
│   └── pubspec.yaml
│
├── docs_firmware/              Firmware documentation
│   ├── README.md               Full firmware technical reference (11 sections)
│   ├── CHANGELOG.md            Firmware version history
│   ├── APP_HANDOFF.md          Figma + Flutter setup guide
│   └── PROGRESS.md             Build progress tracker
│
├── platformio.ini              PlatformIO build config
└── README.md                   This file
```

---

## 3. Hardware

### Dev / Testing Hardware (current)

| Component | Part | Notes |
|-----------|------|-------|
| MCU | ESP32 30-pin dev board (CP2102 USB) | Available on AliExpress |
| IMU | MPU-6050 GY-521 breakout | AD0 floating → address 0x68 |
| I2C SCL | GPIO **22** | 4.7 kΩ pull-up to 3.3V recommended |
| I2C SDA | GPIO **21** | 4.7 kΩ pull-up to 3.3V recommended |
| LED | GPIO **2** | Built-in on dev board |
| Button | GPIO **0** | BOOT button (built-in, active LOW) |

### Final / Production Hardware (target)

| Component | Part | Notes |
|-----------|------|-------|
| MCU | ESP32-S2 WEMOS S2 Mini | Compact, native USB |
| IMU | MPU-6500 GY-6500 breakout | AD0 tied GND → address 0x68 |
| I2C SCL | GPIO **33** | 4.7 kΩ pull-up to 3.3V |
| I2C SDA | GPIO **34** | 4.7 kΩ pull-up to 3.3V |
| LED | GPIO **15** | Built-in on WEMOS S2 Mini |
| Battery | 3.7V LiPo via TP4056 | ~500 mAh recommended |

### Wiring — Dev Kit

```
ESP32 Dev Board          GY-521 (MPU-6050)
─────────────────        ─────────────────
3.3V  ────────────────▶  VCC
GND   ────────────────▶  GND
GPIO22 ───────────────▶  SCL
GPIO21 ───────────────▶  SDA
                         AD0  (leave floating — module has internal pull-down)
```

### Chip Differences (MPU-6050 vs MPU-6500)

| Property | MPU-6050 | MPU-6500 |
|----------|----------|----------|
| WHO_AM_I | `0x68` | `0x70` |
| Temp formula | `raw / 340.0 + 36.53` | `raw / 333.87 + 21.0` |
| Registers / burst read | identical | identical |

---

## 4. Firmware

### 4.1 Build & Flash

**Prerequisites:** VS Code + PlatformIO IDE extension.

**`platformio.ini`** (dev kit):

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

**Build and flash commands:**

```bash
# First time only — upload LittleFS filesystem partition
pio run --target uploadfs

# Upload firmware
pio run --target upload

# Open serial monitor
pio device monitor
```

For the ESP32-S2 WEMOS S2 Mini, the first flash requires manual boot mode:
1. Hold **BOOT** (GPIO 0)
2. Press and release **RESET** while holding BOOT
3. Release BOOT — board is in download mode
4. Run `pio run --target upload`
5. Press RESET after upload to start firmware

### 4.2 Firmware Architecture

```
┌──────────────────────────────────────────────────────────────────┐
│                        SwimTrack Firmware                        │
│                                                                  │
│  ┌──────────┐   raw     ┌────────────┐  filtMag  ┌────────────┐  │
│  │ MPU-6050 │──samples──▶ imu_filters │──────────▶│  stroke_ │  │
│  │ /6500    │  (50 Hz)  │  (EMA 0.3) │           │ detector   │  │
│  └──────────┘           └────────────┘           └─────┬──────┘  │
│       │                       │ filtMag                │ strokes │
│       │ gz (dps)              ▼                        ▼         │
│       │              ┌──────────────┐      ┌─────────────────┐   │
│       └──────────────▶ lap_counter  │      │ session_manager │  │
│                      │ turn FSM     │─lap──▶ (LittleFS JSON) │  │
│                      │ rest detect  │─rest─▶                 │  │
│                      └──────────────┘      └───────┬─────────┘   │
│                                                    │             │
│                      ┌─────────────────────────────▼───────────┐ │
│                      │             wifi_server                 │ │
│                      │  GET /api/live   GET /api/sessions      │ │
│                      │  POST /api/session/start|stop           │ │
│                      │  GET / (web dashboard)                  │ │
│                      └─────────────────────────────────────────┘ │
│                                                                  │
│  main.cpp — State Machine                                        │
│  IDLE ──(btn/API)──▶ RECORDING ──(btn/API)──▶ IDLE              │
│  IDLE ──(5 min, no WiFi client)──▶ SLEEPING ──(btn)──▶ IDLE     │
└──────────────────────────────────────────────────────────────────┘

Phone ◀──── WiFi SoftAP 192.168.4.1 ────▶ REST API + Web Dashboard
```

| Module | Files | Responsibility |
|--------|-------|----------------|
| IMU Driver | `mpu6500.h/.cpp/_part2` | I2C init, 14-byte burst read, unit conversion |
| IMU Filters | `imu_filters.h/.cpp` | EMA filter, accel magnitude |
| Stroke Detector | `stroke_detector.h/.cpp/_part2` | Two-state FSM, dynamic baseline, stroke rate |
| Lap Counter | `lap_counter.h/.cpp/_part2` | Turn FSM, rest detection, variance window |
| Session Manager | `session_manager.h/.cpp/_part2/_part3` | LittleFS mount, JSON serialisation, CRUD |
| WiFi Server | `wifi_server.h`, `wifi_server.cpp`, `wifi_live.cpp`, `wifi_api.cpp` | SoftAP, REST API, dashboard |
| Main | `main.cpp` | State machine, self-test, button, sample loop |

### 4.3 Firmware Build Status

| # | Module | Status | Confirmed Output |
|---|--------|--------|-----------------|
| 1 | IMU Driver | ✅ PASSED | `WHO_AM_I=0x68`, 50.00 Hz confirmed |
| 2 | IMU Filters | ✅ PASSED | Serial Plotter: raw / filtered clean |
| 3 | Stroke Detector | ✅ PASSED | Manual count matches ±1 over 10 strokes |
| 4 | Lap Counter + Rest Detection | ✅ PASSED | Turn + rest confirmed |
| 5 | Session Manager + LittleFS | ✅ PASSED | JSON saved / listed / printed / deleted |
| 6 | WiFi SoftAP + REST API | ✅ PASSED | All endpoints, `clients=1` confirmed |
| 7 | Web Dashboard | ✅ PASSED | Live tiles + start/stop on phone browser |
| 8 | Power Manager | ⏭ SKIPPED | Deferred to ESP32-S2 hardware build |
| 9 | Full Integration + State Machine | ✅ PASSED | Self-test, button, states, 50 Hz stable |
| 10 | Documentation | ✅ COMPLETE | `docs_firmware/README.md` + `CHANGELOG.md` |

### 4.4 Serial Commands

Connect at **115200 baud** (`pio device monitor`).

| Key | Action |
|-----|--------|
| `s` | Start session |
| `x` | Stop + save session to flash |
| `l` | List all saved sessions |
| `p` | Print last session JSON |
| `d` | Delete last session |
| `r` | Reset all counters |
| `f` | Filesystem info (used / total flash) |
| `i` | Print live stats |

**Button (GPIO 0):** Short press = start/stop session · Long press 3s = full reset

**LED patterns:** 2 slow blinks on boot = self-test pass · Rapid blink = fatal error + halt · 2ms flash = stroke detected · 200ms flash = lap confirmed

### 4.5 WiFi REST API

Connect phone to **SwimTrack** / `swim1234` → open `http://192.168.4.1`

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/` | Web dashboard |
| GET | `/api/status` | Device mode, battery, uptime, heap |
| GET | `/api/live` | Real-time IMU + stroke/lap metrics |
| GET | `/api/sessions` | Summary list of all saved sessions |
| GET | `/api/sessions/<id>` | Full session JSON with lap data |
| POST | `/api/session/start` | Body: `{"pool_length_m":25}` |
| POST | `/api/session/stop` | Save and close active session |
| POST | `/api/config` | Update pool length |
| DELETE | `/api/sessions/<id>` | Delete session from flash |

> **Note on float fields:** Several fields in `/api/live` and session JSON are returned as strings (e.g. `"rate_spm": "32.5"`) due to `serialized(String(value, 1))` in the ArduinoJson firmware code. The app parses these with `double.tryParse(v.toString())`.

> **Note on session IDs:** The `id` returned from `/api/session/start` is `millis()` at start time (a temporary handle). The `saved_id` from `/api/session/stop` is the permanent LittleFS numeric ID — always use `saved_id` for subsequent `GET /api/sessions/<id>` calls.

### 4.6 Algorithms

**Stroke detection**
Filtered accel magnitude (EMA α=0.3) is compared against a dynamic baseline tracked by a slow EMA (α=0.02). A stroke is counted on the falling edge after magnitude exceeds `baseline + 0.4g`. A 500ms minimum gap prevents double-counting.

**Lap / turn detection**
Three-state FSM: IDLE → SPIKE (gyro Z > 150 dps for ≥30ms) → GLIDE_WAIT (filtered magnitude < 1.2g for ≥100ms within a 2-second window) → LAP CONFIRMED. A 5-second minimum lap duration (15s for pool use) prevents false triggers.

**Rest detection**
Rolling 1-second variance of filtered magnitude. Variance < 0.05 g² sustained for 5+ seconds → resting state. One-shot `restJustStarted()` / `restJustEnded()` pulse flags are set on transitions.

**SWOLF**
`swolf = stroke_count + lap_duration_seconds`. Lower is better.

### 4.7 config.h Tuning Reference

All algorithm constants live in `include/config.h`. Change these for pool vs. bench testing:

| Constant | Bench | Pool | Description |
|----------|-------|------|-------------|
| `LAP_MIN_DURATION_MS` | `5000` | **`15000`** | ⚠️ **Change before pool use** |
| `STROKE_THRESHOLD_G` | `0.4` | tune | g above baseline to count a stroke |
| `STROKE_MIN_GAP_MS` | `500` | tune | minimum ms between strokes |
| `TURN_GYRO_Z_THRESH_DPS` | `150` | tune | gyro Z spike threshold for turn |
| `REST_VARIANCE_THRESH` | `0.05` | tune | g² variance threshold for rest |
| `GLIDE_ACCEL_THRESH_G` | `1.2` | tune | max accel during glide phase |

### 4.8 Firmware Troubleshooting

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| `WHO_AM_I = 0x00` | I2C not responding | Check SDA/SCL not swapped, VCC=3.3V, GND connected |
| `WHO_AM_I mismatch` | Wrong chip detected | Set `MPU_WHO_AM_I_VAL` to `0x68` (MPU-6050) or `0x70` (MPU-6500) in `config.h` |
| No strokes detected | Threshold too high | Lower `STROKE_THRESHOLD_G` to `0.25`, or raise `EMA_ALPHA` to `0.4` |
| Strokes double-counted | Gap too short | Raise `STROKE_MIN_GAP_MS` to `600` or `700` |
| Lap fires during arm motion | Gyro threshold too low | Raise `TURN_GYRO_Z_THRESH_DPS` to `180–200` |
| LittleFS mount failed | Wrong partition scheme | Add `board_build.filesystem = littlefs` and run `pio run --target uploadfs` |
| WiFi not visible on phone | Boot not complete | Wait 3s; verify `[WIFI] AP started` in Serial Monitor |
| Sporadic `I2C Error 263` | Loose wire | Shorten and secure I2C wires; add 4.7 kΩ pull-ups |

### 4.9 Migration to Final Hardware

Five changes total — everything else is identical.

**`include/config.h`** — 3 changes:

```diff
- #define PIN_I2C_SCL       22
+ #define PIN_I2C_SCL       33

- #define PIN_I2C_SDA       21
+ #define PIN_I2C_SDA       34

- #define MPU_WHO_AM_I_VAL  0x68
+ #define MPU_WHO_AM_I_VAL  0x70

- #define PIN_LED            2
+ #define PIN_LED           15
```

**`src/mpu6500.cpp`** — temperature formula in `read()`:

```diff
- sample.temp_c = static_cast<float>(rawTemp) / 340.0f + 36.53f;
+ sample.temp_c = static_cast<float>(rawTemp) / 333.87f + 21.0f;
```

**`platformio.ini`**:

```diff
- board = esp32dev
+ board = wemos_s2_mini
```

---

## 5. Mobile App

Flutter companion app (Android). Communicates with the device over the local WiFi network — no internet required. Also works in **simulator mode** with no hardware at all.

### 5.1 Tech Stack

| Library | Version | Purpose |
|---------|---------|---------|
| `flutter_riverpod` | ^2.4.0 | State management |
| `go_router` | ^13.0.0 | Navigation |
| `dio` | ^5.4.0 | HTTP client |
| `wifi_iot` | ^0.3.19 | WiFi connection |
| `sqflite` | ^2.3.0 | Local SQLite session storage |
| `shared_preferences` | ^2.2.0 | User profile persistence |
| `fl_chart` | ^0.66.0 | SWOLF trend, lap charts |
| `google_fonts` | ^6.1.0 | Poppins + Inter fonts |
| `intl` | ^0.19.0 | Date formatting |
| `wakelock_plus` | ^1.1.0 | Screen-on during live session |

### 5.2 App Architecture

```
Screens
  Login ──▶ Profile Setup ──▶ Main Shell
                                 ├── Home Tab      (live session, start/stop)
                                 ├── History Tab   (session list → detail)
                                 └── Settings Tab  (profile, device, sync)

Providers (Riverpod)
  DeviceProvider    connection state machine (disconnected / connecting / connected / error)
  LiveProvider      StreamProvider polling /api/live every 1s during recording
  SessionProvider   loads from SQLite, exposes session list
  SettingsProvider  pool length, simulator mode toggle

Services
  DeviceApiService  all HTTP calls; returns mock data in simulator mode
  WiFiService       wifi_iot connect/disconnect wrapper
  SyncService       pulls device sessions → compares with local → inserts new
  DatabaseService   SQLite sessions / laps / rests tables

Design System
  SwimTrackColors   primary #0077B6 · secondary #00B4D8 · background #F8FAFE
  SwimTrackTextStyles   Poppins for numbers/headings · Inter for body/labels
```

### 5.3 Build & Run

**Prerequisites:** Flutter SDK ≥3.0, Android Studio or VS Code with Flutter plugin, Android device or emulator (API 21+).

```bash
cd C:\Dan_WS\SwimTrack\app
flutter pub get
flutter run
```

**Required Android permissions** in `android/app/src/main/AndroidManifest.xml`:

```xml
<uses-permission android:name="android.permission.ACCESS_WIFI_STATE"/>
<uses-permission android:name="android.permission.CHANGE_WIFI_STATE"/>
<uses-permission android:name="android.permission.ACCESS_FINE_LOCATION"/>
<uses-permission android:name="android.permission.ACCESS_COARSE_LOCATION"/>
<uses-permission android:name="android.permission.INTERNET"/>
<uses-permission android:name="android.permission.CHANGE_NETWORK_STATE"/>
<uses-permission android:name="android.permission.ACCESS_NETWORK_STATE"/>
```

Also add to the `<application>` tag (required for plain HTTP to the device):

```xml
android:usesCleartextTraffic="true"
```

**Build release APK:**

```bash
flutter build apk --release
# Output: build/app/outputs/flutter-apk/app-release.apk
```

### 5.4 App Build Status

| # | Module | Status |
|---|--------|--------|
| 1 | Project Setup + Theme + Mock Data | ✅ |
| 2 | Database Service + Session Provider | ✅ |
| 3 | Dashboard Screen | ✅ |
| 4 | Session History + Detail Screens | ✅ |
| 5 | Progress Screen | ✅ |
| 6 | Device API + WiFi Service | ✅ |
| 7 | Settings + Connection Flow | ✅ |
| 8 | Sync Service | ✅ |
| 9 | Live Session Screen | ✅ |
| 10 | Start Session Flow | ✅ |
| 11 | Polish + Error Handling | ✅ |
| 12 | Documentation | ✅ |

### 5.5 Simulator Mode

Enable **Simulator Mode** in the Settings tab. All API calls are replaced by `MockDataService` — the app generates realistic fake sessions, live data, and sync results with no ESP32 required.

Use simulator mode to develop and test the full app UI on any Android device or emulator, independent of hardware availability.

### 5.6 Connecting to Device

1. Flash SwimTrack firmware to the ESP32 (Prompt 9 `main.cpp`)
2. Power on the ESP32
3. On your Android phone: **Settings → WiFi → SwimTrack** (password: `swim1234`)
4. Open the SwimTrack app
5. Login screen: tap **Connect**
6. Settings tab → tap **Sync Sessions** to pull saved sessions from device
7. Home tab → tap **START SESSION** → swim → tap **STOP SESSION**

The entire system operates on the local WiFi network — no internet or cloud required.

### 5.7 App Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| All API calls fail | `usesCleartextTraffic` missing | Add `android:usesCleartextTraffic="true"` to `<application>` in AndroidManifest.xml |
| WiFi permission denied | Android permissions missing | Add all WiFi permissions listed in §5.3 |
| Google Fonts not loading | No internet on first launch | Connect phone to internet once, then fonts cache |
| sqflite crash | Running on iOS simulator | Use a physical Android device |
| Connection refused | Device not powered on or out of range | Verify ESP32 is on; Serial Monitor should show `[WIFI] AP started` |
| Sync shows 0 new sessions | Never synced, or sessions already local | Power on device, connect to WiFi, then tap Sync |

---

## 6. API Reference

Full base URL: `http://192.168.4.1`  
All responses: `Content-Type: application/json`

### GET /api/status

```json
{
  "mode":           "IDLE",
  "session_active": false,
  "wifi_clients":   1,
  "uptime_s":       607,
  "battery_pct":    100,
  "battery_v":      4.2,
  "pool_m":         25,
  "free_heap":      235400
}
```

### GET /api/live

```json
{
  "strokes":        14,
  "rate_spm":       "32.5",
  "stroke_type":    "FREESTYLE",
  "lap_strokes":    5,
  "laps":           2,
  "session_laps":   2,
  "resting":        false,
  "lap_elapsed_s":  "8.3",
  "swolf_est":      "21.8",
  "variance":       "0.0012",
  "session_active": true,
  "ax": "0.012", "ay": "-0.003", "az": "1.001",
  "gx": "0.01",  "gy": "0.02",  "gz": "-0.01",
  "temp_c": "24.5"
}
```

### GET /api/sessions

```json
[
  {
    "id":            12010,
    "duration_s":    86.1,
    "laps":          4,
    "total_strokes": 47,
    "pool_m":        25,
    "total_dist_m":  100,
    "avg_swolf":     "9.7"
  }
]
```

### GET /api/sessions/{id}

```json
{
  "id":            12010,
  "start_ms":      1234567890,
  "end_ms":        1234567976,
  "duration_s":    "86.1",
  "pool_m":        25,
  "laps":          4,
  "total_strokes": 47,
  "total_dist_m":  100,
  "avg_swolf":     "9.7",
  "avg_spm":       "38.4",
  "lap_data": [
    { "n": 1, "t_s": "21.3", "strokes": 5, "swolf": "26.3", "spm": "14.1" }
  ],
  "rests": [
    { "start_ms": 45000, "dur_s": "12.3" }
  ]
}
```

### POST /api/session/start

Request: `{"pool_length_m": 25}` (optional — defaults to current device setting)  
Response: `{"ok": true, "pool_m": 25, "id": 1234567}`

### POST /api/session/stop

Response: `{"ok": true, "saved_id": 12010}`

### POST /api/config

Request: `{"pool_length_m": 50}`  
Response: `{"ok": true, "pool_m": 50}`

### DELETE /api/sessions/{id}

Response: `{"ok": true}` or `{"error": "session not found"}`

---

## 7. Changelog

### Firmware — v1.0.0-dev (March 2026)

First complete firmware release. All core modules implemented and confirmed on real hardware (ESP32 30-pin + MPU-6050). Power Manager (Prompt 8) deferred to ESP32-S2 hardware build. Stub battery values (100%, 4.20V) served via API.

| Version | Description |
|---------|-------------|
| v0.1 | Basic IMU read + Serial at 50 Hz |
| v0.2 | EMA filter + Serial Plotter |
| v0.3 | Stroke detection FSM + LED flash |
| v0.4 | Lap counter + rest detection |
| v0.5 | Session JSON → LittleFS |
| v0.6 | WiFi SoftAP + REST endpoints |
| v0.7 | Web dashboard |
| v0.9 | State machine + self-test + button + sleep |
| v1.0-dev | Full integration. All prompts complete except battery. |

Known issues: stroke type classification always returns FREESTYLE (gyro-based classification deferred); session ID uses `millis()` rather than a real-time clock; `LAP_MIN_DURATION_MS` must be manually changed between bench (5000) and pool (15000) testing.

### App — v1.0.0 (March 2026)

Complete Flutter companion app across 7 build stages: project foundation, login/profile, session history and detail screens, home tab with live recording, settings/device/sync, polish and error handling, and documentation. Simulator mode allows full app testing without hardware.

---

*SwimTrack — built with PlatformIO + Arduino (firmware) and Flutter (app).*  
*Firmware repo: https://github.com/AbdelRahman-Madboly/SwimTrack-Firmware*