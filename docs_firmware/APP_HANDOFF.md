# SwimTrack App — Handoff Guide

## What This Document Is

A complete guide for building the SwimTrack Flutter app from scratch.
Follow these steps in order:

1. Design all screens in Figma (this guide)
2. Set up the Flutter project
3. Create a new Claude project and build the app prompt-by-prompt

---

## Step 1 — Figma: Design All Screens

### Setup
1. Create a new Figma project: **"SwimTrack App"**
2. Frame size: **390 × 844** (iPhone 14 / standard mobile)
3. Install the **Poppins** and **Inter** fonts from Google Fonts

### Color Styles — Create these first

| Style Name | Hex | Where Used |
|------------|-----|------------|
| `primary` | `#0077B6` | AppBar, buttons, active nav, links |
| `secondary` | `#00B4D8` | Charts, FAB, highlights, gradients |
| `background` | `#F8FAFE` | Screen background |
| `card` | `#FFFFFF` | All card backgrounds |
| `text/primary` | `#1A1A2E` | Headings, important numbers |
| `text/secondary` | `#4A4A68` | Body text, labels |
| `text/tertiary` | `#8E8EA0` | Timestamps, units, hints |
| `success` | `#2ECC71` | Improving metrics, good SWOLF, green arrows |
| `warning` | `#E74C3C` | Declining metrics, bad SWOLF, red arrows |
| `neutral` | `#F39C12` | Unchanged metrics, amber |
| `divider` | `#E8EDF2` | Subtle lines between sections |

### Text Styles — Create these second

| Style Name | Font | Size | Weight |
|------------|------|------|--------|
| `metric/large` | Poppins | 36px | Bold |
| `heading/screen` | Poppins | 24px | SemiBold |
| `heading/section` | Poppins | 18px | SemiBold |
| `heading/card` | Inter | 16px | SemiBold |
| `body/regular` | Inter | 14px | Regular |
| `label/small` | Inter | 12px | Regular |
| `label/tiny` | Inter | 10px | Regular |

### Component Library — Build these before screens

**Card component** — white, border-radius 16px, shadow (0 2px 8px rgba(0,0,0,0.08)), padding 16px

**Metric card** — 3-column row of small cards. Each: icon top, large number middle, label bottom.

**Bottom navigation** — 4 tabs: Dashboard (house icon), History (list icon), Progress (chart icon), Settings (gear icon). Active tab uses `primary` color.

**Connection dot** — 8px circle: green = connected, grey = disconnected, pulsing amber = connecting.

**SWOLF badge** — number + arrow. Green arrow down = improving. Red arrow up = declining.

**Stroke type icon** — swimmer emoji or icon for: 🏊 Freestyle · 🏊‍ Backstroke · 🤸 Breaststroke · 🦋 Butterfly

---

### Screen 1 — Dashboard (design this first, most important)

```
┌──────────────────────────────┐
│ ≡  SwimTrack        [●] 85%  │  ← AppBar: hamburger, title, battery+dot
├──────────────────────────────┤
│                              │
│  Good morning, Swimmer! 🏊   │  ← Greeting, Inter 16px
│                              │
│  ┌────────────────────────┐  │
│  │  Latest Session        │  │
│  │  Wed Mar 20 · Freestyle│  │  ← Date + stroke icon
│  │  200m · 8 laps         │  │  ← Distance + laps
│  │  SWOLF  43  ↓2         │  │  ← SWOLF + green arrow (improving)
│  │  Duration: 8:00        │  │
│  └────────────────────────┘  │
│                              │
│  ┌────────┐┌────────┐┌──────┐│
│  │   3    ││  600m  ││  41  ││  ← 3 small metric cards
│  │ swims  ││  dist  ││ best ││
│  │ /week  ││  /week ││SWOLF ││
│  └────────┘└────────┘└──────┘│
│                              │
│  SWOLF Trend                 │  ← Section header
│  ┌────────────────────────┐  │
│  │  📈  line chart        │  │  ← fl_chart gradient fill
│  │  last 10 sessions      │  │  ← x=session#, y=avg SWOLF
│  └────────────────────────┘  │
│                              │
│              [🔄 Sync]       │  ← FAB, secondary color
│                              │
│  [🏠]  [📋]  [📊]  [⚙️]    │  ← Bottom nav
└──────────────────────────────┘
```

**Design notes:**
- Latest session card uses `card` background, shadow, border-radius 16
- SWOLF trend arrow: green (#2ECC71) if lower than previous, red (#E74C3C) if higher
- Chart gradient: primary (#0077B6) top, secondary (#00B4D8) bottom, 15% opacity fill under line
- Metric cards are equal width, gap 8px between them

---

### Screen 2 — Live Session

```
┌──────────────────────────────┐
│  ←     RECORDING  🔴         │  ← Back arrow, red pulsing dot
├──────────────────────────────┤
│                              │
│          03 : 24             │  ← Timer, Poppins 48px bold
│                              │
│  ┌────────────────────────┐  │
│  │       STROKES          │  │  ← Label, Inter 12px secondary
│  │          42            │  │  ← Big number, Poppins 64px bold primary
│  │    🏊  Freestyle       │  │  ← Stroke type icon + name
│  └────────────────────────┘  │
│                              │
│  ┌──────────┐ ┌────────────┐ │
│  │  Lap  3  │ │  34.2 spm  │ │  ← Current lap | stroke rate
│  │  12 str  │ │  SWOLF: 46 │ │  ← Strokes | SWOLF
│  └──────────┘ └────────────┘ │
│                              │
│  ┌────────────────────────┐  │
│  │                        │  │
│  │      ⏹  STOP           │  │  ← Large red button, min height 64px
│  │                        │  │
│  └────────────────────────┘  │
└──────────────────────────────┘
```

**Design notes:**
- Dark background (#1A1A2E) for this screen only — focused mode
- Timer in white, huge
- SWOLF and rate in secondary color (#00B4D8)
- Stop button: red (#E74C3C), full width, large
- Add subtle pulsing animation hint on the recording dot

---

### Screen 3 — Session Detail

```
┌──────────────────────────────┐
│  ←  Session Detail           │
├──────────────────────────────┤
│  Wed Mar 20, 2026            │  ← Date, Poppins 18 semibold
│  8:00 min  ·  200m  ·  25m pool│ ← Subtitle row
│                              │
│  ┌──────┐┌──────┐┌──────┐┌──┐│
│  │  43  ││ 33.2 ││ 128  ││30s││  ← 4 metric cards
│  │ SWOLF││ spm  ││strokes││rest│  ← Labels
│  └──────┘└──────┘└──────┘└──┘│
│                              │
│  SWOLF per Lap               │
│  ┌────────────────────────┐  │
│  │  📈 line/bar chart     │  │
│  └────────────────────────┘  │
│                              │
│  Stroke Rate per Lap         │
│  ┌────────────────────────┐  │
│  │  📈 line chart         │  │
│  └────────────────────────┘  │
│                              │
│  Lap Breakdown               │
│  ┌────────────────────────┐  │
│  │ # │Type│Str│ Time│SWOLF│  │
│  │ 1 │ 🏊 │ 16│28.4s│ 44 │  │  ← SWOLF cell: green if good
│  │ 2 │ 🏊 │ 15│27.1s│ 42 │  │
│  │ 3 │ 🏊 │ 17│29.2s│ 46 │  │  ← red if above avg
│  └────────────────────────┘  │
└──────────────────────────────┘
```

---

### Screen 4 — Session History

```
┌──────────────────────────────┐
│  History              🔄     │  ← Sync icon top right
├──────────────────────────────┤
│  ↓ Pull to refresh           │  ← Hint text
│                              │
│  ┌────────────────────────┐  │
│  │  🏊  Wed Mar 20        │  │  ← Stroke icon + date
│  │  200m · 8 laps         │  │
│  │  SWOLF 43  · 8:00      │  │  ← SWOLF + duration
│  └────────────────────────┘  │
│  ┌────────────────────────┐  │
│  │  🏊  Tue Mar 19        │  │
│  │  150m · 6 laps         │  │
│  │  SWOLF 45  · 6:20      │  │
│  └────────────────────────┘  │
│  ... more sessions           │
└──────────────────────────────┘
```

---

### Screen 5 — Progress

```
┌──────────────────────────────┐
│  Progress                    │
├──────────────────────────────┤
│  [Week] [Month] [All Time]   │  ← Tab selector
│                              │
│  SWOLF Trend                 │
│  ┌────────────────────────┐  │
│  │  📈 sessions over time │  │
│  └────────────────────────┘  │
│                              │
│  Distance per Week           │
│  ┌────────────────────────┐  │
│  │  📊 bar chart          │  │
│  └────────────────────────┘  │
│                              │
│  Stroke Distribution         │
│  ┌────────────────────────┐  │
│  │  🥧 pie chart          │  │
│  └────────────────────────┘  │
│                              │
│  Personal Bests              │
│  🏆 Best SWOLF: 38           │
│  🏆 Fastest lap: 24.1s       │
│  🏆 Longest session: 2000m   │
└──────────────────────────────┘
```

---

### Screen 6 — Settings

```
┌──────────────────────────────┐
│  Settings                    │
├──────────────────────────────┤
│  DEVICE                      │  ← Section header
│  ┌────────────────────────┐  │
│  │  ● Connected           │  │  ← Green dot + status
│  │  SwimTrack  192.168.4.1│  │
│  │  Battery: 85%  FW 1.0.0│  │
│  │  [Disconnect]          │  │
│  └────────────────────────┘  │
│                              │
│  POOL                        │
│  ┌────────────────────────┐  │
│  │  Pool Length  [25m ▼]  │  │  ← Dropdown/picker
│  └────────────────────────┘  │
│                              │
│  APP                         │
│  ┌────────────────────────┐  │
│  │  Simulator Mode  [  ○] │  │  ← Toggle
│  │  Units          [m ▼]  │  │
│  └────────────────────────┘  │
│                              │
│  ABOUT                       │
│  SwimTrack v1.0.0            │
│  Firmware v1.0.0             │
└──────────────────────────────┘
```

---

### Figma Export Checklist

After designing all 6 screens, export each at **2× resolution** as PNG:

| File Name | Screen |
|-----------|--------|
| `01_dashboard.png` | Dashboard |
| `02_live_session.png` | Live Session |
| `03_session_detail.png` | Session Detail |
| `04_history.png` | Session History |
| `05_progress.png` | Progress |
| `06_settings.png` | Settings |

Save all to `app/figma/` folder.

---

## Step 2 — Flutter Project Setup

Follow `app/SETUP.md` exactly:

```bash
cd C:\Dan_WS\SwimTrack\app
flutter create . --project-name swimtrack --org com.swimtrack
copy pubspec_template.yaml pubspec.yaml
flutter pub get
flutter run
```

Add these permissions to `android/app/src/main/AndroidManifest.xml`:
```xml
<uses-permission android:name="android.permission.ACCESS_WIFI_STATE"/>
<uses-permission android:name="android.permission.CHANGE_WIFI_STATE"/>
<uses-permission android:name="android.permission.ACCESS_FINE_LOCATION"/>
<uses-permission android:name="android.permission.ACCESS_COARSE_LOCATION"/>
<uses-permission android:name="android.permission.INTERNET"/>
<uses-permission android:name="android.permission.CHANGE_NETWORK_STATE"/>
<uses-permission android:name="android.permission.ACCESS_NETWORK_STATE"/>
```

---

## Step 3 — Create New Claude Project

1. Go to [claude.ai](https://claude.ai) and create a **new Project**
2. Name it: **SwimTrack App**
3. Upload these files as **Project Knowledge**:
   - `app/INSTRUCTIONS.md` ← Claude reads this as context every message
   - `app/PROMPTS.md`
   - `app/DESIGN_BRIEF.md`
   - `app/figma/01_dashboard.png` ← your Figma exports
   - `app/figma/02_live_session.png`
   - `app/figma/03_session_detail.png`
   - `app/figma/04_history.png`
   - `app/figma/05_progress.png`
   - `app/figma/06_settings.png`

4. **System prompt / project instructions** — paste the content of `app/INSTRUCTIONS.md`

---

## Step 4 — Build the App Prompt by Prompt

In the new Claude project, send prompts one at a time from `app/PROMPTS.md`.

**Start with this message:**
```
I am building the SwimTrack Flutter app. My project setup is complete (flutter create done, pubspec.yaml updated, flutter pub get done). 

Please implement Prompt 1: Project Setup + Theme + Mock Data.

Reference the design system from INSTRUCTIONS.md and match the visual style from the uploaded Figma screenshots.
```

**After each prompt:**
- Run `flutter run` and test
- Fix any errors before moving to the next prompt
- Send the next prompt only after the previous one passes

**Tip — reference the Figma screenshots:**
```
"Build the Dashboard screen matching 01_dashboard.png"
"The session card should look like the one in 03_session_detail.png"
```

---

## Device API Quick Reference (for app development)

The firmware is running on the ESP32 and serves this API at `http://192.168.4.1`:

```
GET  /api/status
     → {"mode":"IDLE","battery_pct":85,"session_active":false,"firmware_version":"1.0.0"}

GET  /api/live
     → {"stroke_count":14,"lap_count":2,"current_swolf":42,"stroke_rate":32.5,
        "elapsed_sec":145,"is_resting":false,"accel":{"x":0.1,"y":-0.03,"z":0.98}}

GET  /api/sessions
     → [{"id":"12010","lap_count":4,"distance_m":100,"avg_swolf":9.7,"duration_sec":22}]

GET  /api/sessions/12010
     → full session JSON with lap_data[] array

POST /api/session/start    body: {"pool_length_m":25}
POST /api/session/stop
DELETE /api/sessions/12010
```

**Simulator mode:** When `Settings > Use Simulator` is ON, all API calls are replaced by `mock_data_service.dart` — no device needed for development.

---

## App Build Order

| # | Prompt | What Gets Built | Test |
|---|--------|----------------|------|
| 1 | Setup + Theme | App boots, theme, bottom nav, mock data | `flutter run` shows app |
| 2 | DB + Provider | SQLite, 5 mock sessions persist | Sessions survive restart |
| 3 | Dashboard | Latest session card, charts, metrics | Charts render correctly |
| 4 | History + Detail | List + tap to detail, charts, lap table | Navigation works |
| 5 | Progress | SWOLF trend, distance bars, pie chart | Date filter works |
| 6 | Device API | HTTP client, WiFi connect, simulator | Graceful error without device |
| 7 | Settings | Connection flow, pool picker, toggles | Simulator toggle works |
| 8 | Sync Service | Pull sessions from device → SQLite | New sessions appear |
| 9 | Live Session | Real-time polling, timer, stop → detail | Full live flow works |
| 10 | Start Flow | Dashboard → Start → Live → Stop → Detail | End-to-end cycle |
| 11 | Polish | Empty states, loading, errors, animations | All edge cases covered |
| 12 | Docs | README, code comments | Documentation complete |

---

## Connecting App to Real Device

1. Flash the SwimTrack firmware to ESP32 (Prompt 9 `main.cpp`)
2. Power on the ESP32
3. On your phone, go to WiFi Settings
4. Connect to: **SwimTrack** / password: **swim1234**
5. In the app, go to Settings → tap **Connect**
6. The app connects to `http://192.168.4.1` automatically
7. Go to Dashboard → tap **Sync** to pull sessions
8. Tap **Start** → confirm pool length → session begins live
