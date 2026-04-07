# SwimTrack — ESP32 Swim Training Computer + Flutter App

> A wrist-worn swimming tracker that detects strokes, counts laps, calculates SWOLF, stores sessions on flash, and streams live data over WiFi to a companion mobile app.

---

## Repositories

| Project | Repository |
|---------|-----------|
| **Firmware** (ESP32 / PlatformIO) | https://github.com/AbdelRahman-Madboly/SwimTrack-Firmware.git |
| **App** (Flutter / Android) | https://github.com/AbdelRahman-Madboly/SwimTrack-app.git |

---

## Table of Contents

1. [System Overview](#1-system-overview)
2. [Hardware](#2-hardware)
3. [Firmware — Setup & Build](#3-firmware--setup--build)
4. [Firmware — How It Works](#4-firmware--how-it-works)
5. [Firmware — Tuning for Accuracy](#5-firmware--tuning-for-accuracy)
6. [App — Setup & Build](#6-app--setup--build)
7. [App — Building the APK](#7-app--building-the-apk)
8. [App — Adding the App Icon](#8-app--adding-the-app-icon)
9. [Connecting Device + App](#9-connecting-device--app)
10. [WiFi REST API Reference](#10-wifi-rest-api-reference)
11. [Known Limitations & How to Improve](#11-known-limitations--how-to-improve)
12. [Troubleshooting](#12-troubleshooting)

---

## 1. System Overview

```
┌────────────────────────────────────────────────────────────┐
│                  SwimTrack Architecture                    │
├─────────────────────┬──────────────────────────────────────┤
│   ESP32 (Firmware)  │   Android Phone (Flutter App)        │
│                     │                                      │
│  MPU-6500 IMU       │   Settings tab                       │
│     ↓ 50 Hz         │     └─ Connect to SwimTrack WiFi     │
│  EMA Filter         │     └─ Sync sessions                 │
│     ↓               │                                      │
│  Stroke Detector    │   Home tab                           │
│     ↓               │     └─ START SESSION                 │
│  Lap Counter        │     └─ Live metrics (1 Hz poll)      │
│     ↓               │     └─ STOP SESSION                  │
│  Session Manager    │                                      │
│     ↓ LittleFS      │   History tab                        │
│  WiFi SoftAP        │     └─ Tap session → detail          │
│  REST API ──────────┼──→  SWOLF chart, lap table           │
│  http://192.168.4.1 │                                      │
└─────────────────────┴──────────────────────────────────────┘
```

The device runs as a WiFi Access Point (`SwimTrack` / `swim1234`). The phone connects to this network and communicates exclusively over local HTTP — no internet required.

---

## 2. Hardware

| Component | Development Board | Final Hardware |
|-----------|------------------|----------------|
| MCU | ESP32 30-pin dev board | LOLIN S2 Mini (ESP32-S2) |
| IMU | MPU-6050 (GY-521) | MPU-6500 (GY-6500) |
| I2C SCL | GPIO 22 | GPIO 33 |
| I2C SDA | GPIO 21 | GPIO 34 |
| LED | GPIO 2 | GPIO 15 |
| Button | GPIO 0 (BOOT) | GPIO 0 (BOOT) |
| WHO\_AM\_I | 0x68 | 0x70 |

**Wiring (LOLIN S2 Mini):**

```
MPU-6500 VCC → 3.3V
MPU-6500 GND → GND
MPU-6500 SCL → GPIO 33
MPU-6500 SDA → GPIO 34
```

---

## 3. Firmware — Setup & Build

### Prerequisites

- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- USB cable connected to the ESP32

### Clone & Flash

```bash
git clone https://github.com/AbdelRahman-Madboly/SwimTrack-Firmware.git
cd SwimTrack-Firmware

# 1. Upload filesystem (first time only — stores web dashboard)
pio run --target uploadfs

# 2. Flash firmware
pio run --target upload

# 3. Open serial monitor (115200 baud)
pio device monitor
```

### platformio.ini (final board)

```ini
[env:lolin_s2_mini]
platform    = espressif32
board       = lolin_s2_mini
framework   = arduino
lib_deps    = bblanchon/ArduinoJson@^7.0.0
board_build.filesystem = littlefs
monitor_speed = 115200
```

### Serial Commands

| Key | Action |
|-----|--------|
| `s` | Start session |
| `x` | Stop + save session |
| `l` | List all saved sessions |
| `p` | Print last session JSON |
| `d` | Delete last session |
| `r` | Reset stroke/lap counters |
| `f` | Filesystem info |
| `i` | Print live stats |

**Button (GPIO 0):** Short press = start/stop session · Long press 3 s = full reset

**LED:** 2 slow blinks = boot OK · Rapid = fatal error · 2 ms flash = stroke · 200 ms flash = lap

---

## 4. Firmware — How It Works

### File Structure

```
SwimTrack-Firmware/
├── include/
│   ├── config.h          ← All tuning constants (edit this to tune)
│   ├── mpu6500.h         ← IMU driver interface
│   ├── imu_filters.h     ← EMA filter
│   ├── stroke_detector.h ← Stroke FSM
│   ├── lap_counter.h     ← Turn + rest detection
│   ├── session_manager.h ← LittleFS session storage
│   └── wifi_server.h     ← HTTP server + API routes
└── src/
    ├── main.cpp               ← State machine, 50 Hz loop
    ├── mpu6500.cpp/.._part2   ← I2C read, calibration
    ├── imu_filters.cpp        ← EMA implementation
    ├── stroke_detector.cpp/..2← Two-state stroke FSM
    ├── lap_counter.cpp/..2    ← Turn FSM, rest detection
    ├── session_manager.cpp/..3← JSON build + LittleFS CRUD
    ├── wifi_server.cpp        ← SoftAP + route registration
    ├── wifi_live.cpp          ← GET /api/live handler
    └── wifi_api.cpp           ← All other API handlers
```

### Algorithm Pipeline

**1. IMU Sampling (50 Hz)**
Raw accel + gyro read from MPU-6500 over I2C. EMA filter with α = 0.3 smooths noise.

**2. Stroke Detection**
Two-state FSM on EMA-filtered acceleration magnitude:
- State IDLE → STROKE when magnitude exceeds `baseline + STROKE_THRESHOLD_G (0.4g)`
- Counted on falling edge back to IDLE
- Minimum gap between strokes: `STROKE_MIN_GAP_MS (500 ms)` — prevents double-count

**3. Lap / Turn Detection**
Monitors gyro Z axis for wall-turn signature:
- Spike > `TURN_GYRO_Z_THRESH_DPS (150 °/s)` for minimum `TURN_SPIKE_MIN_MS`
- Followed by glide period > `TURN_GLIDE_MIN_MS (200 ms)` with low accel
- Lap counted only if elapsed since last lap > `LAP_MIN_DURATION_MS (5000 ms)`

**4. Rest Detection**
Tracks rolling variance of accel magnitude over a 2 s window:
- Variance < `REST_VARIANCE_THRESH (0.05)` for `REST_DURATION_MS (5000 ms)` → rest declared
- Variance rising → rest ends, new lap segment begins

**5. Session Storage**
Sessions serialised to JSON and written to LittleFS (4 MB flash partition).
Up to 80 laps and 20 rest intervals per session.

---

## 5. Firmware — Tuning for Accuracy

All tuning constants are in `include/config.h`. **Edit this file to improve detection accuracy.**

### Stroke Detection

| Constant | Default | When to Change |
|----------|---------|----------------|
| `STROKE_THRESHOLD_G` | `0.4f` | Increase if getting phantom strokes at rest; decrease if missing strokes |
| `STROKE_MIN_GAP_MS` | `500` | Increase for slower swimmers; decrease for sprinters |
| `EMA_ALPHA` | `0.3f` | Decrease (e.g. 0.15) for more smoothing; increase for faster response |

**If the app shows strokes when not moving:**
Increase `STROKE_THRESHOLD_G` to `0.6` or `0.8`. The device is picking up motion noise at rest.

**If strokes are being missed:**
Decrease `STROKE_THRESHOLD_G` to `0.25`. Check that the wrist-band holds the device snugly — loose mounting causes weak peaks.

### Lap Detection

| Constant | Default | When to Change |
|----------|---------|----------------|
| `TURN_GYRO_Z_THRESH_DPS` | `150.0f` | Decrease if turns not detected; increase if false laps appear |
| `LAP_MIN_DURATION_MS` | `5000` | Must be < minimum possible lap time. Increase for casual swimmers |
| `TURN_GLIDE_WINDOW_MS` | `2000` | Increase for swimmers with longer push-off glide |
| `GLIDE_ACCEL_THRESH_G` | `1.2f` | Threshold during glide to confirm turn — usually fine as-is |

**For bench testing (short fake laps):**
Temporarily set `LAP_MIN_DURATION_MS` to `2000` to allow quick test laps.
**Reset to `5000`+ before pool use.**

### Rest Detection

| Constant | Default | When to Change |
|----------|---------|----------------|
| `REST_VARIANCE_THRESH` | `0.05f` | Increase if rests not detected; decrease if activity triggers rest |
| `REST_DURATION_MS` | `5000` | Decrease for interval training with short rests |

---

## 6. App — Setup & Build

### Prerequisites

- Flutter SDK ≥ 3.0.0 ([install](https://docs.flutter.dev/get-started/install))
- Android Studio or VS Code with Flutter extension
- Android device (physical recommended) or emulator

### Clone & Run

```bash
git clone https://github.com/AbdelRahman-Madboly/SwimTrack-app.git
cd SwimTrack-app
flutter pub get
flutter run
```

### Android Permissions

Ensure `android/app/src/main/AndroidManifest.xml` has these inside `<manifest>`:

```xml
<uses-permission android:name="android.permission.ACCESS_WIFI_STATE"/>
<uses-permission android:name="android.permission.CHANGE_WIFI_STATE"/>
<uses-permission android:name="android.permission.ACCESS_FINE_LOCATION"/>
<uses-permission android:name="android.permission.ACCESS_COARSE_LOCATION"/>
<uses-permission android:name="android.permission.INTERNET"/>
<uses-permission android:name="android.permission.CHANGE_NETWORK_STATE"/>
<uses-permission android:name="android.permission.ACCESS_NETWORK_STATE"/>
```

And inside the `<application>` tag:

```xml
android:usesCleartextTraffic="true"
```

> This is required because the ESP32 serves plain HTTP (not HTTPS).

### App File Structure

```
lib/
├── config/
│   ├── constants.dart      ← API base URL, prefs keys, defaults
│   ├── routes.dart         ← GoRouter setup + auth redirect
│   └── theme.dart          ← Colors, text styles
├── models/
│   ├── device_status.dart  ← DeviceStatus.fromJson()
│   ├── live_data.dart      ← LiveData.fromJson() — handles float-as-string
│   ├── session.dart        ← Session + Lap + RestInterval models
│   └── user_profile.dart   ← Swimmer profile
├── providers/
│   ├── device_provider.dart   ← Connection state machine
│   ├── live_provider.dart     ← StreamProvider — 1 Hz poll of /api/live
│   ├── session_provider.dart  ← SQLite session CRUD + sync
│   ├── profile_provider.dart  ← SharedPreferences user profile
│   └── settings_provider.dart ← Pool length + simulator toggle
├── screens/
│   ├── login_screen.dart
│   ├── profile_setup_screen.dart
│   ├── main_screen.dart         ← 3-tab shell
│   ├── home_tab.dart            ← Idle + recording states
│   ├── history_tab.dart
│   ├── session_detail_screen.dart
│   └── settings_tab.dart
├── services/
│   ├── device_api_service.dart  ← All HTTP calls to ESP32
│   ├── database_service.dart    ← SQLite via sqflite
│   ├── mock_data_service.dart   ← Fake data for simulator mode
│   ├── sync_service.dart        ← Device → local sync logic
│   └── wifi_service.dart        ← WiFi connect/disconnect
├── widgets/                     ← Reusable UI components
└── main.dart                    ← Entry point, simulator sync
```

### Simulator Mode

The app has a built-in **Simulator Mode** (Settings → APP section → toggle).

When ON:
- No physical device or WiFi needed
- All API calls return realistic mock data instantly
- Full app flow works: connect → sync → start session → live metrics → stop → history

**This is for testing and development only.** When connecting to the real device, ensure **Simulator Mode is OFF**.

> **Important fix in `main.dart`:** The app pre-loads the simulator mode preference on startup so `DeviceApiService` is correctly configured before any provider initialises. Without this, the service could use real HTTP calls even while the provider shows simulator mode.

---

## 7. App — Building the APK

### Release APK (for distribution)

```bash
cd SwimTrack-app

# Clean build (recommended)
flutter clean
flutter pub get

# Build release APK
flutter build apk --release

# Output location:
# build/app/outputs/flutter-apk/app-release.apk
```

Install directly on an Android phone:

```bash
flutter install
# or copy the APK to the phone and open it
# (Allow installation from unknown sources if prompted)
```

### Split APKs by architecture (smaller file size)

```bash
flutter build apk --split-per-abi --release
# Creates separate APKs for arm64-v8a, armeabi-v7a, x86_64
# Use arm64-v8a for most modern Android phones
```

### App Bundle (for Google Play Store)

```bash
flutter build appbundle --release
# Output: build/app/outputs/bundle/release/app-release.aab
```

---

## 8. App — Adding the App Icon

### Quick Setup

1. Create the directory `assets/icon/` in the project root
2. Place your icon PNG (1024×1024, square, no transparency for background) at:
   - `assets/icon/app_icon.png` — full icon with background
   - `assets/icon/app_icon_foreground.png` — foreground only (transparent bg) for Android adaptive icons

3. Add `flutter_launcher_icons` to `pubspec.yaml` dev dependencies:

```yaml
dev_dependencies:
  flutter_launcher_icons: ^0.13.1

flutter_launcher_icons:
  android: true
  ios: true
  image_path: "assets/icon/app_icon.png"
  min_sdk_android: 21
  adaptive_icon_background: "#03045E"
  adaptive_icon_foreground: "assets/icon/app_icon_foreground.png"
```

4. Run the generator:

```bash
dart run flutter_launcher_icons
```

5. Build the APK — your icon will appear on the Android home screen.

> A ready-made icon (swimmer silhouette + wave, SwimTrack brand colors) is included in this repository's `assets/icon/` folder.

---

## 9. Connecting Device + App

### Step-by-Step

1. **Flash firmware** to the ESP32 (see Section 3)
2. **Power on** the ESP32 — LED blinks twice = self-test passed
3. **On your Android phone:** Settings → WiFi → connect to `SwimTrack` (password: `swim1234`)
4. **Open the SwimTrack app**
5. **Login screen:** Device Name = `SwimTrack`, Password = `swim1234` → tap **Connect**
6. **Settings tab:** tap **Sync Sessions** to pull any previously saved sessions
7. **Home tab:** tap **START SESSION** → swim → tap **STOP SESSION**
8. Session is saved locally and appears in History

> The phone may occasionally drop the SwimTrack WiFi (no internet = Android deprioritises it). If the app shows "Disconnected", go to phone WiFi settings and reconnect to `SwimTrack`.

### Verify the Device is Sending Real Data

When connected, open a browser on the same phone and navigate to:

```
http://192.168.4.1/api/live
```

You should see a JSON response with `"session_active": false` when idle. Start a session (button short press or serial `s`) and refresh — you'll see live stroke/lap data updating. This confirms the device is sending real data, not mock data.

---

## 10. WiFi REST API Reference

**Base URL:** `http://192.168.4.1`  
**Auth:** None  
**Protocol:** HTTP/1.1, JSON bodies

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

`mode` is either `"IDLE"` or `"RECORDING"`.

### GET /api/live

Poll at 1 Hz during a session. All float fields arrive as strings (e.g. `"32.5"` not `32.5`) due to ArduinoJson serialization — the app handles both.

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
  "session_active": true,
  "ax": "0.012", "ay": "-0.003", "az": "1.001",
  "gx": "0.01",  "gy": "0.02",   "gz": "-0.01"
}
```

### GET /api/sessions

Returns summary list (no lap data):

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

Full session with lap data:

```json
{
  "id":            12010,
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

```json
// Request
{ "pool_length_m": 25 }

// Response
{ "ok": true, "pool_m": 25, "id": 1234567 }
```

`id` is `millis()` at start time — temporary handle. Use `saved_id` from stop response to fetch the saved session.

### POST /api/session/stop

```json
// Response
{ "ok": true, "saved_id": 12010 }
```

### POST /api/config

```json
// Request
{ "pool_length_m": 50 }

// Response
{ "ok": true, "pool_m": 50 }
```

### DELETE /api/sessions/{id}

```json
// Response
{ "ok": true }
```

---

## 11. Known Limitations & How to Improve

This section is for developers who want to improve the system.

### 1. Stroke Classification Always Returns FREESTYLE

**File:** `src/stroke_detector.cpp` — `_classifyStroke()`

Currently returns `FREESTYLE` for all strokes. To implement real classification:
- Collect labelled IMU data for each stroke type
- Breaststroke has distinctive bilateral symmetry in accel Y axis
- Backstroke shows inverted Z compared to freestyle
- Butterfly has strong double-peak pattern per cycle
- A simple rule-based classifier or small decision tree would work

### 2. Battery Reading Always Returns 100%

**File:** `include/config.h` — `PIN_BATTERY_ADC`

The ADC read is defined but not connected to a voltage divider circuit. To fix:
- Connect a 100k+100k voltage divider from battery+ to GPIO 1
- The existing constants `BATT_FULL_MV`, `BATT_EMPTY_MV`, `BATT_DIVIDER_RATIO` are correct
- Uncomment/implement the ADC read in `session_manager.cpp`

### 3. Session Timestamps Are Relative (Not Real Time)

**Root cause:** The ESP32 has no RTC and no NTP (no internet). `millis()` gives time since boot, not Unix time.

**Options to fix:**
- Add a DS3231 RTC module connected over I2C (most reliable)
- On WiFi connect from the phone, send current timestamp via `POST /api/config` with `{"timestamp": 1234567890}` and store it as an offset

### 4. Android WiFi Switching (Android 10+)

Android 10+ blocks apps from programmatically switching WiFi networks. Users must manually connect to `SwimTrack` in phone settings before using the app.

**Mitigation:** The app's Login screen shows clear instructions. The `wifi_iot` plugin does its best but cannot override the OS policy.

### 5. Lap Detection: LAP_MIN_DURATION_MS Needs Pool Tuning

The default `5000 ms` minimum lap duration works for 25m pools but may need adjustment:
- **Sprint lanes (15m drill laps):** Set to `3000`
- **50m pool:** `8000` to avoid false laps mid-pool
- **Children / slow swimmers:** `8000`+

### 6. SWOLF Accuracy

SWOLF = strokes + seconds per lap. Accuracy depends on:
- Stroke threshold being set correctly for the swimmer's power
- The device being worn firmly on the wrist (not sliding)
- Lap detection being accurate

**Verification:** After a session, compare the app's lap times with a stopwatch for a few laps. If lap times are off, tune `TURN_GYRO_Z_THRESH_DPS`.

### 7. Live Data Continuity

The app polls `/api/live` at 1 Hz. If the phone WiFi sleeps briefly, a poll may fail and the UI shows a momentary "---". This is cosmetic — the device keeps recording correctly.

---

## 12. Troubleshooting

### App shows data when device is not moving

**Cause:** Simulator Mode is ON.  
**Fix:** Settings → APP section → turn Simulator Mode **OFF**, then reconnect.

### All API calls fail / connection refused

1. Verify your phone is connected to the `SwimTrack` WiFi network (not home/mobile)
2. Check `AndroidManifest.xml` has `android:usesCleartextTraffic="true"` in `<application>`
3. Try `http://192.168.4.1/api/status` in Chrome on the phone — if it loads, the firmware is fine

### App shows "Disconnected" immediately after connecting

Android deprioritises WiFi networks without internet. Go to phone WiFi settings, tap `SwimTrack` → "Stay connected" (or equivalent for your Android version).

### WiFi permission denied

Add all permissions from Section 6 to `AndroidManifest.xml`. On Android 13+, `ACCESS_FINE_LOCATION` is required for WiFi scanning.

### Google Fonts not loading (blank text)

The app needs internet access on first run to download Poppins/Inter. Once downloaded they are cached. Or add the fonts as local assets in `pubspec.yaml`.

### sqflite crash on iOS Simulator

Use a physical Android device or an Android emulator. sqflite has known issues with the iOS Simulator.

### App shows no sessions after sync

1. Start a session on the device (button press or serial `s`)
2. Stop it (button press or serial `x`)
3. Verify with `l` in serial monitor that sessions are listed
4. In app: Settings → Sync Sessions

### Firmware fails to compile

- Ensure `board_build.filesystem = littlefs` is in `platformio.ini`
- Run `pio run --target uploadfs` before `pio run --target upload` on first flash
- If `WHO_AM_I` mismatch: check `MPU_WHO_AM_I_VAL` in `config.h` matches your hardware (0x68 for MPU-6050, 0x70 for MPU-6500)

### LED shows rapid blink (fatal error)

Serial monitor will show the error. Common causes:
- IMU not found at I2C address (check wiring)
- LittleFS mount failed (run `pio run --target uploadfs`)

---

## Firmware Version History

See [CHANGELOG.md](CHANGELOG.md) in the firmware repo.

---

## License

MIT — see `LICENSE` in each repository.

---

*SwimTrack — built for swimmers, by swimmers.*