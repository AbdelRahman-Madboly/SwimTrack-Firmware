# SwimTrack Firmware

> ESP32-S2 firmware for the SwimTrack wrist-worn swim training computer.  
> Part of the SwimTrack open-source project.

---

## Project Links

| Repository | Contents |
|------------|----------|
| **This repo** | ESP32-S2 firmware |
| [SwimTrack App](https://github.com/AbdelRahman-Madboly/SwimTrack-app.git) | Flutter Android companion app |
| [SwimTrack Data Collection](https://github.com/AbdelRahman-Madboly/SwimTrack-Data_Collection.git) | ESP-NOW IMU collector + Python analysis + stroke classifier |

---

## What is SwimTrack?

SwimTrack is a wrist-worn swim training computer. It reads an MPU-6500 IMU sensor at 50 Hz, detects swim strokes using a dynamic-threshold FSM, counts laps via gyroscope turn detection, calculates SWOLF efficiency scores, and stores full session data to on-device flash. A phone connects over the device's own WiFi access point and communicates via a REST API.

**SWOLF** = strokes per lap + seconds per lap. Lower is better.

---

## Hardware

### Required components

| Component | Part | Notes |
|-----------|------|-------|
| MCU | LOLIN S2 Mini (ESP32-S2) | Single-core, 240 MHz, native USB |
| IMU | GY-6500 (MPU-6500 breakout) | AD0 tied to GND → I2C address 0x68 |
| Battery | 3.7 V LiPo, ~500 mAh | Via TP4056 charger board |

### Wiring

```
LOLIN S2 Mini            GY-6500 (MPU-6500)
─────────────────        ──────────────────
3.3V  ────────────────▶  VCC
GND   ────────────────▶  GND
GPIO33 ───────────────▶  SCL
GPIO34 ───────────────▶  SDA
GND   ────────────────▶  AD0   (tie LOW to fix address at 0x68)
```

### Battery voltage monitoring (optional)

A **100 kΩ + 100 kΩ voltage divider** from the LiPo positive terminal to **GPIO1** allows the firmware to report battery percentage. Without this circuit the firmware returns 0% and the app hides the battery icon.

```
LiPo+ ──┬── 100kΩ ──┬── GPIO1 (ADC)
         │           │
        (not         100kΩ
       connected     │
       here)        GND
```

> **Status:** The battery ADC feature is implemented in firmware but the voltage divider hardware has not been assembled yet. Battery percentage will read 0% until the circuit is soldered.

---

## Architecture

```
┌──────────────────────────────────────────────────────────────────┐
│                       SwimTrack Firmware                          │
│                                                                    │
│  ┌──────────┐  50 Hz  ┌────────────┐  filtMag  ┌──────────────┐  │
│  │ MPU-6500 │────────▶│ imu_filters│──────────▶│    stroke_   │  │
│  │  (I2C)   │         │  EMA 0.3   │           │   detector   │  │
│  └──────────┘         └────────────┘           └──────┬───────┘  │
│       │                     │                         │ stroke    │
│       │ gz                  │ filtMag                 │ count     │
│       │                     ▼                         ▼           │
│       │           ┌───────────────┐       ┌─────────────────────┐ │
│       └──────────▶│  lap_counter  │       │   session_manager   │ │
│                   │  turn FSM     │──lap──▶   LittleFS JSON     │ │
│                   │  rest detect  │──rest─▶                     │ │
│                   └───────────────┘       └──────────┬──────────┘ │
│                                                      │             │
│  ┌───────────────────────────────────────────────────▼──────────┐ │
│  │                       wifi_server                              │ │
│  │   GET /api/live    GET /api/sessions    GET / (dashboard)      │ │
│  │   POST /api/session/start|stop          DELETE /api/sessions   │ │
│  └────────────────────────────────────────────────────────────────┘ │
│                                                                    │
│  ┌──────────────────────────────────────────────────────────────┐ │
│  │               main.cpp — State Machine                         │ │
│  │   IDLE ──(button/API)──▶ RECORDING ──(button/API)──▶ IDLE     │ │
│  │   IDLE ──(5 min, no WiFi client)──▶ SLEEPING ──(button)──▶ IDLE│ │
│  └──────────────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────────────┘

Phone  ◀────  WiFi SoftAP 192.168.4.1  ────▶  REST API + Dashboard
```

---

## File Map

| File | Role |
|------|------|
| `config.h` | All pins, thresholds, and constants — single source of truth |
| `mpu6500.h/.cpp/_part2.cpp` | I2C driver: init, 14-byte burst read, unit conversion |
| `imu_filters.h/.cpp` | EMAFilter class, `accelMagnitude()` helper |
| `stroke_detector.h/.cpp/_part2.cpp` | Two-state FSM, dynamic baseline, stroke rate, decision-tree classifier |
| `lap_counter.h/.cpp/_part2.cpp` | Turn FSM, rest detection, variance window, LapRecord |
| `session_manager.h/.cpp/_part2/_part3.cpp` | LittleFS session lifecycle, JSON serialisation, CRUD |
| `wifi_server.h/.cpp` | SoftAP init, route table, PROGMEM web dashboard |
| `wifi_live.cpp` | `GET /api/live` handler |
| `wifi_api.cpp` | All POST / DELETE / GET session handlers |
| `main.cpp` | `setup()`, `loop()`, state machine, button, serial commands |

---

## Build and Flash

### Prerequisites

- [VS Code](https://code.visualstudio.com/) with the [PlatformIO IDE extension](https://platformio.org/install/ide?install=vscode)

### Steps

```bash
git clone https://github.com/AbdelRahman-Madboly/SwimTrack-Firmware.git
cd SwimTrack-Firmware
# Open in VS Code → PlatformIO will install the ESP32-S2 toolchain automatically
```

1. Open the project in VS Code
2. Select the `lolin_s2_mini` environment in PlatformIO
3. Click **Upload** (or `pio run --target upload`)
4. Open **Serial Monitor** at 115200 baud

Expected boot output:
```
==========================================
  SwimTrack FW v2.0.0
  Commands: s x l p d r f i
==========================================
[SELF-TEST] IMU ... PASS (WHO_AM_I=0x70)
[SELF-TEST] LittleFS ... PASS
[SELF-TEST] ALL PASS v
[WIFI] AP started. Connect to "SwimTrack" (pass: swim1234)
[WIFI] Dashboard at http://192.168.4.1/
[MAIN] Ready. State=IDLE  Pool=25m
```

---

## WiFi Access Point

| Setting | Value |
|---------|-------|
| SSID | `SwimTrack` |
| Password | `swim1234` |
| IP address | `192.168.4.1` |
| Port | 80 |

A web dashboard is served at `http://192.168.4.1/` and works in any phone browser without the app installed.

---

## REST API

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/` | Embedded HTML dashboard |
| GET | `/api/status` | Mode, battery, uptime, pool length, free heap |
| GET | `/api/live` | Real-time IMU + stroke/lap/SWOLF snapshot |
| GET | `/api/sessions` | Array of session summaries |
| GET | `/api/sessions/{id}` | Full session JSON with per-lap data |
| POST | `/api/session/start` | Body: `{"pool_length_m":25}` — starts recording |
| POST | `/api/session/stop` | Saves session to LittleFS |
| POST | `/api/config` | Runtime config update (pool length) |
| DELETE | `/api/sessions/{id}` | Delete one session |
| OPTIONS | `*` | CORS preflight |

### `/api/live` response example

```json
{
  "strokes": 14,
  "rate_spm": "32.5",
  "stroke_type": "FREESTYLE",
  "lap_strokes": 5,
  "laps": 2,
  "resting": false,
  "lap_elapsed_s": "18.4",
  "swolf_est": "23.4",
  "lap_dps": "1.79",
  "session_active": true,
  "session_laps": 2,
  "batt_pct": 0,
  "batt_mv": 0
}
```

> Float fields are serialised as strings (e.g. `"32.5"` not `32.5`). The Flutter app handles this via its `_d()` helper in `LiveData.fromJson()`.

---

## Serial Commands

While connected via USB serial monitor (115200 baud):

| Key | Action |
|-----|--------|
| `s` | Start recording session |
| `x` | Stop and save session |
| `l` | List all sessions on flash |
| `p` | Print last saved session |
| `d` | Delete last saved session |
| `r` | Reset stroke/lap counters |
| `f` | Print LittleFS filesystem info |
| `i` | Print live stats snapshot |

---

## Stroke Detection Algorithm

The detector uses a **two-state FSM** (BELOW / ABOVE) with a **dynamic baseline**:

1. A slow EMA (α = 0.02) tracks the quiet gravity level — updated only in BELOW state so arm swings do not inflate the baseline
2. Dynamic threshold = baseline + `STROKE_THRESHOLD_G` (0.095 g)
3. Rising edge (BELOW → ABOVE): must pass a 500 ms minimum gap guard
4. Falling edge (ABOVE → BELOW): stroke counted, stroke type classified

### Stroke Classifier

A depth-2 decision tree trained on real dry-land IMU data (99.2% cross-validation accuracy):

```
ax_min ≤ -0.35          →  BACKSTROKE
ax_min >  -0.35  AND  gx_max ≤ 482.42  →  FREESTYLE
ax_min >  -0.35  AND  gx_max >  482.42  →  BACKSTROKE
```

Where `ax_min` and `gx_max` are computed over the last 50 samples (1 second window at 50 Hz). See the [Data Collection repo](https://github.com/AbdelRahman-Madboly/SwimTrack-Data_Collection.git) for training details.

---

## Tunable Thresholds (`config.h`)

| Define | Value | Description |
|--------|-------|-------------|
| `STROKE_THRESHOLD_G` | `0.0950f` | Stroke detection threshold above baseline [g] |
| `STROKE_MIN_GAP_MS` | `500` | Minimum ms between strokes |
| `TURN_GYRO_Z_THRESH_DPS` | `150.0f` | Gyro Z threshold for lap turn detection [dps] |
| `LAP_MIN_DURATION_MS` | `15000` | Minimum lap duration for pool use [ms] |
| `REST_VARIANCE_THRESH` | `0.16421f` | Mag variance below which = resting [g²] |
| `REST_DURATION_MS` | `5000` | Duration below threshold before rest is declared [ms] |

All three detection thresholds (`STROKE_THRESHOLD_G`, `REST_VARIANCE_THRESH`, `LAP_MIN_DURATION_MS`) were updated from hand-guessed values to data-driven values using the dry-land dataset in the Data Collection repo.

---

## Known Limitations

- **No RTC:** Session IDs and timestamps use `millis()` since boot, not real wall-clock time. A DS3231 RTC module over I2C would fix this.
- **Battery circuit not assembled:** `batt_pct` always returns 0 until the 100kΩ/100kΩ voltage divider is wired to GPIO1.
- **Dry-land classifier only:** The stroke classifier was trained on dry-land data. Pool data (with water resistance changing wrist dynamics) may reduce accuracy. Pool validation is the next planned step.
- **Breaststroke / Butterfly:** Only freestyle and backstroke are in the training data. Both other strokes return the closest matching class.
- **Single swimmer only:** One SoftAP client at a time is the expected use case.