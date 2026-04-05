# SwimTrack

ESP32-based wrist-worn swim training computer + Flutter companion app.

Detects strokes, counts laps, calculates SWOLF, stores sessions on flash, streams live data over WiFi, and syncs to a phone app.

---

## Project Structure

```
SwimTrack/
├── src/                    ← Firmware source (PlatformIO)
├── include/                ← Firmware headers
├── app/                    ← Flutter mobile app
│   ├── lib/
│   ├── INSTRUCTIONS.md     ← Claude project instructions for app build
│   ├── PROMPTS.md          ← 12 app build prompts
│   ├── SETUP.md            ← Flutter setup guide
│   └── DESIGN_BRIEF.md     ← Figma design guide
├── docs/
│   ├── README.md           ← Full firmware documentation (11 sections)
│   └── CHANGELOG.md        ← Version history
├── PROGRESS.md             ← Build progress tracker
└── platformio.ini
```

---

## Hardware

| Component | Dev / Testing | Final Product |
|-----------|--------------|---------------|
| MCU | ESP32 30-pin dev | ESP32-S2 WEMOS S2 Mini |
| IMU | MPU-6050 (GY-521) | MPU-6500 (GY-6500) |
| I2C SCL | GPIO 22 | GPIO 33 |
| I2C SDA | GPIO 21 | GPIO 34 |
| LED | GPIO 2 | GPIO 15 |
| WHO_AM_I | 0x68 | 0x70 |

---

## Firmware Build Status

| # | Module | Status |
|---|--------|--------|
| 1 | IMU Driver | ✅ |
| 2 | IMU Filters | ✅ |
| 3 | Stroke Detector | ✅ |
| 4 | Lap Counter + Rest | ✅ |
| 5 | Session Manager + LittleFS | ✅ |
| 6 | WiFi SoftAP + REST API | ✅ |
| 7 | Web Dashboard | ✅ |
| 8 | Power Manager | ⏭ Skipped |
| 9 | Full Integration | ✅ |
| 10 | Documentation | ✅ |

---

## Firmware Quick Start

```ini
# platformio.ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
lib_deps =
    bblanchon/ArduinoJson@^7.0.0
board_build.filesystem = littlefs
monitor_speed = 115200
```

```bash
pio run --target uploadfs   # first time only
pio run --target upload
pio device monitor
```

---

## WiFi REST API

Connect to **SwimTrack** / `swim1234` → `http://192.168.4.1`

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/` | Web dashboard |
| GET | `/api/status` | Mode, battery, uptime, clients |
| GET | `/api/live` | Real-time IMU + swim metrics |
| GET | `/api/sessions` | Session list |
| GET | `/api/sessions/<id>` | Full session JSON |
| POST | `/api/session/start` | `{"pool_length_m":25}` |
| POST | `/api/session/stop` | Save session |
| POST | `/api/config` | Update settings |
| DELETE | `/api/sessions/<id>` | Delete session |

---

## Serial Commands

| Key | Action |
|-----|--------|
| `s` | Start session |
| `x` | Stop + save |
| `l` | List sessions |
| `p` | Print last JSON |
| `d` | Delete last |
| `r` | Reset counters |
| `f` | Flash info |
| `i` | Live stats |

**Button (GPIO 0):** Short press = start/stop · Long press 3s = full reset

---

## Algorithm Summary

**Stroke:** EMA-filtered magnitude > baseline + 0.4g, counted on falling edge, 500ms minimum gap.

**Lap:** Gyro Z > 150 dps for >30ms, followed by accel < 1.2g for >100ms within 2s.

**Rest:** Rolling 1s variance < 0.05 g² for 5+ seconds.

**SWOLF:** stroke_count + lap_duration_seconds (lower = better)

---

## Mobile App

Built in Flutter. Communicates with the device over WiFi REST API. Stores sessions locally in SQLite. See `app/` folder for all app files.

**App features:**
- Dashboard with SWOLF trend chart
- Live session screen (polls /api/live at 1Hz)
- Session history with lap-by-lap breakdown
- Progress charts (SWOLF trend, distance, stroke distribution)
- WiFi sync to pull sessions from device
- Simulator mode — works without physical device

---

## Migration to Final Hardware (ESP32-S2 + MPU-6500)

Five-line change in `config.h` and `mpu6500.cpp`. See `docs/README.md` Section 11.

```diff
- #define PIN_I2C_SCL       22
+ #define PIN_I2C_SCL       33
- #define PIN_I2C_SDA       21
+ #define PIN_I2C_SDA       34
- #define MPU_WHO_AM_I_VAL  0x68
+ #define MPU_WHO_AM_I_VAL  0x70
# In mpu6500.cpp read():
- sample.temp_c = rawTemp / 340.0f + 36.53f;
+ sample.temp_c = rawTemp / 333.87f + 21.0f;
# In platformio.ini:
- board = esp32dev
+ board = wemos_s2_mini
```