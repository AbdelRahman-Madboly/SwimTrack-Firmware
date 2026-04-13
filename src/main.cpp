/**
 * @file    main.cpp
 * @brief   SwimTrack firmware — Full Integration (production).
 *
 *  States:  IDLE | RECORDING | SLEEPING
 *  Button:  Short press = start/stop  |  Long press 3 s = full reset
 *  Sleep:   Auto deep-sleep after 5 min idle with no WiFi client
 *
 *  v2 fixes:
 *    1. Added apiStartRecording() / apiStopRecording() — public wrappers called
 *       by wifi_api.cpp when the app starts/stops via REST.  Previously the API
 *       called sessMgr.startSession() directly, leaving s_state=IDLE so the 50 Hz
 *       loop never ran stroke/lap detection (strokes always 0 in log).
 *    2. analogSetWidth(12) called in setup() — the ESP32-S2 Arduino core may
 *       default to 13-bit mode (max 8191) causing the battery formula to return
 *       13202 mV.  Forcing 12-bit keeps ADC_MAX_COUNT=4095 correct.
 *    3. strokeDet.update() passes lastSample.ax and lastSample.gz for classifier.
 *
 * @author  SwimTrack Firmware Team
 * @date    2026-04
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include "config.h"
#include "mpu6500.h"
#include "imu_filters.h"
#include "stroke_detector.h"
#include "lap_counter.h"
#include "session_manager.h"
#include "wifi_server.h"

// ============================================================
//  FIRMWARE VERSION
// ============================================================

#define FW_VERSION   "2.0.0"
#define FW_BUILD     __DATE__ " " __TIME__

// ============================================================
//  BUTTON CONFIG
// ============================================================

#define BTN_DEBOUNCE_MS    50UL
#define BTN_LONG_PRESS_MS  3000UL
#define SLEEP_WAKEUP_PIN   GPIO_NUM_0

// ============================================================
//  DEVICE STATE MACHINE
// ============================================================

enum class DeviceState : uint8_t {
    IDLE      = 0,
    RECORDING = 1,
    SLEEPING  = 2
};

static DeviceState s_state = DeviceState::IDLE;

static const char* stateName(DeviceState st) {
    switch (st) {
        case DeviceState::IDLE:      return "IDLE";
        case DeviceState::RECORDING: return "RECORDING";
        case DeviceState::SLEEPING:  return "SLEEPING";
        default:                     return "UNKNOWN";
    }
}

// ============================================================
//  GLOBALS  (extern'd in wifi_server.h)
// ============================================================

MPU6500        imu;
EMAFilter      magFilter(EMA_ALPHA);
StrokeDetector strokeDet;
LapCounter     lapCtr;
SessionManager sessMgr;
IMUSample      lastSample;

uint32_t lapStrokeCount = 0;
uint8_t  poolLengthM    = DEFAULT_POOL_LENGTH_M;
uint32_t restStartMs    = 0;

// ============================================================
//  STATICS
// ============================================================

static uint32_t s_nextSampleMs   = 0;
static uint32_t s_sampleCount    = 0;
static uint32_t s_rateWindowMs   = 0;
static uint32_t s_lastClientMs   = 0;
static uint32_t s_btnPressMs     = 0;
static bool     s_btnWasPressed  = false;

static const uint32_t RATE_CHECK_MS = 5000UL;

// ============================================================
//  LED HELPERS
// ============================================================

static void ledFlash(uint32_t ms)
{
    digitalWrite(PIN_LED, HIGH);
    delay(ms);
    digitalWrite(PIN_LED, LOW);
}

static void ledBlink(uint8_t count, uint32_t onMs, uint32_t offMs)
{
    for (uint8_t i = 0; i < count; i++) {
        digitalWrite(PIN_LED, HIGH); delay(onMs);
        digitalWrite(PIN_LED, LOW);  delay(offMs);
    }
}

// ============================================================
//  SELF-TEST
// ============================================================

static void selfTest()
{
    Serial.println();
    Serial.println("==========================================");
    Serial.printf( "  SwimTrack FW v%s\n", FW_VERSION);
    Serial.printf( "  Built: %s\n", FW_BUILD);
    Serial.printf( "  Free heap: %u bytes\n", ESP.getFreeHeap());
    Serial.println("  Commands: s x l p d r f i");
    Serial.println("==========================================");

    bool pass = true;

    Serial.print("[SELF-TEST] IMU ... ");
    if (imu.begin()) {
        Serial.printf("PASS (WHO_AM_I=0x%02X)\n", imu.whoAmI());
    } else {
        Serial.println("FAIL — check SDA/SCL wiring");
        pass = false;
    }

    Serial.print("[SELF-TEST] LittleFS ... ");
    if (sessMgr.begin()) {
        Serial.println("PASS");
    } else {
        Serial.println("FAIL — filesystem error");
        pass = false;
    }

    if (pass) {
        Serial.println("[SELF-TEST] ALL PASS v");
        ledBlink(2, 300, 300);
    } else {
        Serial.println("[SELF-TEST] FAILED — halting");
        while (true) { ledFlash(100); delay(100); }
    }
}

// ============================================================
//  STATE TRANSITIONS  (called by button handler AND API wrappers)
// ============================================================

static void enterRecording()
{
    if (s_state == DeviceState::RECORDING) return;

    strokeDet.reset();
    lapCtr.reset();
    lapStrokeCount = 0;
    restStartMs    = 0;
    magFilter.reset(1.0f);

    sessMgr.startSession(poolLengthM);
    s_state = DeviceState::RECORDING;

    ledBlink(3, 80, 80);
    DBG("STATE", "-> RECORDING (pool=%d m)", poolLengthM);
    Serial.printf("[STATE] -> RECORDING (pool=%dm)\n", poolLengthM);
}

static void enterIdle()
{
    if (s_state == DeviceState::RECORDING && sessMgr.isActive()) {
        sessMgr.stopSession(strokeDet.strokeCount());
    }
    s_state        = DeviceState::IDLE;
    s_lastClientMs = millis();

    ledBlink(1, 500, 0);
    DBG("STATE", "-> IDLE");
    Serial.println("[STATE] -> IDLE");
}

static void enterSleep()
{
    Serial.println("[STATE] -> SLEEPING. Press BOOT to wake.");
    Serial.flush();

    if (sessMgr.isActive()) {
        sessMgr.stopSession(strokeDet.strokeCount());
    }

    ledBlink(5, 50, 50);
    WiFi.mode(WIFI_OFF);
    delay(100);
    esp_sleep_enable_ext0_wakeup(SLEEP_WAKEUP_PIN, 0);
    esp_deep_sleep_start();
}

// ============================================================
//  API WRAPPERS  (called by wifi_api.cpp via extern)
//
//  These bridge the REST API to the state machine so that
//  starting/stopping via the app sets s_state correctly.
//  Without these, the 50 Hz loop sees IDLE and skips all
//  stroke/lap detection — strokes count stays 0.
// ============================================================

void apiStartRecording() { enterRecording(); }
void apiStopRecording()  { enterIdle();      }

// ============================================================
//  BUTTON HANDLER
// ============================================================

static void handleButton()
{
    static bool lastState = HIGH;
    bool cur = digitalRead(PIN_BUTTON);

    if (lastState == HIGH && cur == LOW) {
        s_btnPressMs    = millis();
        s_btnWasPressed = true;
    }

    if (lastState == LOW && cur == HIGH && s_btnWasPressed) {
        s_btnWasPressed = false;
        const uint32_t held = millis() - s_btnPressMs;

        if (held >= BTN_LONG_PRESS_MS) {
            if (sessMgr.isActive()) sessMgr.stopSession(strokeDet.strokeCount());
            strokeDet.reset();
            lapCtr.reset();
            lapStrokeCount = 0;
            restStartMs    = 0;
            s_state        = DeviceState::IDLE;
            Serial.println("[BTN] Long press — full reset");
            ledBlink(5, 100, 50);
        } else if (held >= BTN_DEBOUNCE_MS) {
            if (s_state == DeviceState::IDLE)      enterRecording();
            else if (s_state == DeviceState::RECORDING) enterIdle();
        }
    }

    lastState = cur;
}

// ============================================================
//  SERIAL COMMANDS
// ============================================================

static void handleSerial()
{
    if (!Serial.available()) return;
    const char cmd = static_cast<char>(Serial.read());

    switch (cmd) {
        case 's': enterRecording(); break;
        case 'x': enterIdle();      break;
        case 'l': sessMgr.listSessions(); break;

        case 'p':
            if (sessMgr.lastSavedId()) sessMgr.printSession(sessMgr.lastSavedId());
            else Serial.println("[CMD] No saved session.");
            break;

        case 'd':
            if (sessMgr.lastSavedId()) sessMgr.deleteSession(sessMgr.lastSavedId());
            else Serial.println("[CMD] No saved session.");
            break;

        case 'r':
            strokeDet.reset(); lapCtr.reset();
            lapStrokeCount = 0; restStartMs = 0;
            Serial.println("[CMD] Counters reset.");
            break;

        case 'f': sessMgr.printFSInfo(); break;

        case 'i':
            Serial.printf(
                "[STATS] state=%s | laps=%u | strokes=%lu | lapStr=%lu"
                " | rate=%.1fspm | type=%s | rest=%s | var=%.4f"
                " | elapsed=%.1fs | lastDPS=%.2f | clients=%d\n",
                stateName(s_state),
                lapCtr.lapCount(),
                (unsigned long)strokeDet.strokeCount(),
                (unsigned long)lapStrokeCount,
                strokeDet.strokeRateSpm(),
                strokeTypeName(strokeDet.strokeType()),
                lapCtr.isResting() ? "YES" : "NO",
                lapCtr.currentVariance(),
                lapCtr.currentLapElapsedS(),
                lapCtr.lastLap().dps_m_per_stroke,
                (int)WiFi.softAPgetStationNum());
            break;

        default:
            if (cmd >= 0x20)
                Serial.printf("[CMD] Unknown '%c'. Known: s x l p d r f i\n", cmd);
            break;
    }
}

// ============================================================
//  SETUP
// ============================================================

void setup()
{
    Serial.begin(115200);
    delay(300);

    pinMode(PIN_LED,    OUTPUT);
    pinMode(PIN_BUTTON, INPUT_PULLUP);
    digitalWrite(PIN_LED, LOW);

    // Force 12-bit ADC resolution (0–4095).
    // The ESP32-S2 Arduino core may default to 13-bit (0–8191),
    // which causes the battery formula to read ~13 V instead of ~4 V.
    analogSetWidth(12);

    esp_sleep_wakeup_cause_t wakeup = esp_sleep_get_wakeup_cause();
    if (wakeup == ESP_SLEEP_WAKEUP_EXT0) {
        Serial.println("[MAIN] Woke from deep sleep via button.");
    }

    selfTest();

    magFilter.reset(1.0f);
    strokeDet.begin();
    lapCtr.begin();

    wifiBegin(&sessMgr, &strokeDet, &lapCtr, &lastSample);

    Serial.printf("[MAIN] Ready. State=%s  Pool=%dm\n",
                  stateName(s_state), poolLengthM);
    Serial.println("[MAIN] Short press BOOT = start/stop | Long press 3s = reset");

    const uint32_t now = millis();
    s_nextSampleMs  = now;
    s_rateWindowMs  = now;
    s_lastClientMs  = now;
}

// ============================================================
//  LOOP
// ============================================================

void loop()
{
    const uint32_t now = millis();

    wifiLoop();
    handleSerial();
    handleButton();

    if (WiFi.softAPgetStationNum() > 0) {
        s_lastClientMs = now;
    }

    if (s_state == DeviceState::IDLE &&
        !sessMgr.isActive() &&
        (now - s_lastClientMs) >= SLEEP_TIMEOUT_MS) {
        enterSleep();
    }

    // ---- 50 Hz IMU sample gate ----------------------------------------
    if (now >= s_nextSampleMs) {
        s_nextSampleMs += SAMPLE_PERIOD_MS;
        if (now > s_nextSampleMs + SAMPLE_PERIOD_MS)
            s_nextSampleMs = now + SAMPLE_PERIOD_MS;

        if (imu.read(lastSample)) {
            s_sampleCount++;
            const float filtMag = magFilter.update(accelMagnitude(lastSample));

            if (s_state == DeviceState::RECORDING) {

                // Stroke detection — pass ax and gz for classifier window
                if (strokeDet.update(filtMag, lastSample.ax, lastSample.gz, now)) {
                    lapStrokeCount++;
                    Serial.printf(
                        "[STROKE] #%lu | Lap:%u | %.1fspm | %s\n",
                        (unsigned long)strokeDet.strokeCount(),
                        lapCtr.lapCount() + 1u,
                        strokeDet.strokeRateSpm(),
                        strokeTypeName(strokeDet.strokeType()));
                }

                // Lap / turn detection
                if (lapCtr.update(lastSample.gz, filtMag,
                                  lapStrokeCount,
                                  strokeDet.strokeRateSpm(), now)) {
                    const LapRecord& lr = lapCtr.lastLap();
                    sessMgr.recordLap(lr);
                    Serial.printf(
                        "[LAP] #%u | strokes=%lu | t=%.1fs | swolf=%.1f | dps=%.2f\n",
                        lr.lap_number,
                        (unsigned long)lr.stroke_count,
                        lr.duration_s, lr.swolf, lr.dps_m_per_stroke);
                    lapStrokeCount = 0;
                }

                // Rest detection
                bool resting = lapCtr.isResting();
                if (resting && restStartMs == 0) {
                    restStartMs = now;
                } else if (!resting && restStartMs > 0) {
                    float restDurS = (float)(now - restStartMs) / 1000.0f;
                    sessMgr.recordRest(restStartMs, restDurS);
                    restStartMs = 0;
                    DBG("REST", "Ended, duration=%.1f s", restDurS);
                }

            } else {
                // IDLE — still run lapCtr for /api/live variance
                lapCtr.update(lastSample.gz, filtMag, 0, 0.0f, now);
            }

        } else {
            DBG("MAIN", "WARNING: imu.read() failed");
        }
    }

    // ---- Rate diagnostic every 5 s ------------------------------------
    if (now - s_rateWindowMs >= RATE_CHECK_MS) {
        const float elapsed = (float)(now - s_rateWindowMs) / 1000.0f;
        const float hz      = (float)s_sampleCount / elapsed;
        DBG("MAIN", "Sample rate: %.1f Hz | state=%s | strokes=%lu",
            hz, stateName(s_state), (unsigned long)strokeDet.strokeCount());
        s_sampleCount  = 0;
        s_rateWindowMs = now;
    }
}