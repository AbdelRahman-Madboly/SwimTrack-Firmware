/**
 * @file    main.cpp
 * @brief   SwimTrack firmware v1.0.0 — Prompt 9: Full Integration + State Machine.
 *
 *  States:  IDLE | RECORDING | SLEEPING
 *  Button:  Short press = start/stop  |  Long press 3s = full reset
 *  Sleep:   Auto deep-sleep after 5 min idle with no WiFi client
 *  Boot:    Self-test prints on every power-on
 *
 * @author  SwimTrack Firmware Team
 * @date    2025-01-01
 */

#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "mpu6500.h"
#include "imu_filters.h"
#include "stroke_detector.h"
#include "lap_counter.h"
#include "session_manager.h"
#include "wifi_server.h"

//  FIRMWARE VERSION

#define FW_VERSION   "1.0.0"
#define FW_BUILD     __DATE__ " " __TIME__

//  SLEEP / BUTTON CONFIG

#define SLEEP_TIMEOUT_MS        (5UL * 60UL * 1000UL)

#define SLEEP_WAKEUP_PIN        GPIO_NUM_0

#define BTN_DEBOUNCE_MS         50

#define BTN_LONG_PRESS_MS       3000

//  DEVICE STATE MACHINE

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

//  GLOBALS  (accessed via extern in wifi_server.h)

MPU6500        imu;
EMAFilter      magFilter(EMA_ALPHA);
StrokeDetector strokeDet;
LapCounter     lapCtr;
SessionManager sessMgr;
IMUSample      lastSample;

uint32_t lapStrokeCount = 0;
uint8_t  poolLengthM    = DEFAULT_POOL_LENGTH_M;
uint32_t restStartMs    = 0;

//  STATICS

static uint32_t s_nextSampleMs    = 0;
static uint32_t s_sampleCount     = 0;
static uint32_t s_rateWindowMs    = 0;
static uint32_t s_lastImuPrintMs  = 0;
static uint32_t s_lastClientMs    = 0;  ///< Last time a WiFi client was seen
static uint32_t s_btnPressMs      = 0;  ///< millis() when button went LOW
static bool     s_btnWasPressed   = false;

static const uint32_t RATE_CHECK_MS = 5000UL;
static const uint32_t IMU_PRINT_MS  = 10000UL;

//  LED HELPERS

static void ledFlash(uint32_t ms)
{ digitalWrite(PIN_LED, HIGH); delay(ms); digitalWrite(PIN_LED, LOW); }

static void ledBlink(uint8_t count, uint32_t onMs, uint32_t offMs) {
    for (uint8_t i = 0; i < count; i++) {
        digitalWrite(PIN_LED, HIGH); delay(onMs);
        digitalWrite(PIN_LED, LOW);  delay(offMs);
    }
}

//  SELF-TEST

/**
 * @brief Run startup self-test: IMU + LittleFS + print version.
 *        LED pattern: 2 slow blinks = pass, rapid blink = fail + halt.
 * @return true if all checks pass.
 */
static bool selfTest() {
    Serial.println("[SELF-TEST] ========================");
    Serial.printf( "[SELF-TEST] SwimTrack FW v%s\n", FW_VERSION);
    Serial.printf( "[SELF-TEST] Built: %s\n", FW_BUILD);
    Serial.printf( "[SELF-TEST] Free heap: %u bytes\n", ESP.getFreeHeap());
    Serial.println("[SELF-TEST] ========================");

    bool pass = true;

    // 1. IMU check
    Serial.print("[SELF-TEST] IMU ... ");
    if (imu.begin()) {
        Serial.printf("PASS (WHO_AM_I=0x%02X)\n", imu.whoAmI());
    } else {
        Serial.println("FAIL — check SDA/SCL wiring");
        pass = false;
    }

    // 2. LittleFS check
    Serial.print("[SELF-TEST] LittleFS ... ");
    if (sessMgr.begin()) {
        Serial.println("PASS");
    } else {
        Serial.println("FAIL — filesystem error");
        pass = false;
    }

    // 3. Result
    if (pass) {
        Serial.println("[SELF-TEST] ALL PASS ✓");
        ledBlink(2, 300, 300);   // 2 slow blinks = OK
    } else {
        Serial.println("[SELF-TEST] FAILED — halting");
        pinMode(PIN_LED, OUTPUT);
        while (true) { ledFlash(100); delay(100); }
    }

    return pass;
}

//  STATE TRANSITIONS

/**
 * @brief Enter RECORDING state — start session, reset counters.
 */
static void enterRecording() {
    if (s_state == DeviceState::RECORDING) return;

    strokeDet.reset();
    lapCtr.reset();
    lapStrokeCount = 0;
    restStartMs    = 0;
    magFilter.reset(1.0f);

    sessMgr.startSession(poolLengthM);
    s_state = DeviceState::RECORDING;

    ledBlink(3, 80, 80);   // 3 fast blinks = recording started
    Serial.printf("[STATE] → RECORDING (pool=%dm)\n", poolLengthM);
}

/**
 * @brief Enter IDLE state — stop + save session if active.
 */
static void enterIdle() {
    if (s_state == DeviceState::RECORDING && sessMgr.isActive()) {
        sessMgr.stopSession(strokeDet.strokeCount());
    }

    s_state        = DeviceState::IDLE;
    s_lastClientMs = millis();   // reset inactivity timer

    ledBlink(1, 500, 0);   // 1 long blink = idle
    Serial.println("[STATE] → IDLE");
}

/**
 * @brief Enter deep sleep — configure GPIO wakeup and sleep.
 *        Button (GPIO 0, active LOW) wakes the device.
 */
static void enterSleep() {
    Serial.println("[STATE] → SLEEPING");
    Serial.println("[STATE] Press BOOT button to wake.");
    Serial.flush();

    // Auto-save if session was open
    if (sessMgr.isActive()) {
        sessMgr.stopSession(strokeDet.strokeCount());
    }

    ledBlink(5, 50, 50);   // 5 rapid blinks = going to sleep

    WiFi.mode(WIFI_OFF);
    delay(100);

    esp_sleep_enable_ext0_wakeup(SLEEP_WAKEUP_PIN, 0);  // wake on LOW (button)
    esp_deep_sleep_start();
    // Execution does not return from here
}

//  BUTTON HANDLER

/**
 * @brief Non-blocking button state machine.
 *
 *        Reads PIN_BUTTON (active LOW, internal pull-up).
 *        Short press  → toggle IDLE/RECORDING.
 *        Long press   → full reset (counters cleared, session stopped).
 */
static void handleButton() {
    static bool lastState = HIGH;
    bool cur = digitalRead(PIN_BUTTON);

    // Falling edge — button pressed
    if (lastState == HIGH && cur == LOW) {
        s_btnPressMs    = millis();
        s_btnWasPressed = true;
    }

    // Rising edge — button released
    if (lastState == LOW && cur == HIGH && s_btnWasPressed) {
        s_btnWasPressed = false;
        uint32_t held = millis() - s_btnPressMs;

        if (held >= BTN_LONG_PRESS_MS) {
            // Long press — full reset
            if (sessMgr.isActive()) sessMgr.stopSession(strokeDet.strokeCount());
            strokeDet.reset(); lapCtr.reset();
            lapStrokeCount = 0; restStartMs = 0;
            s_state = DeviceState::IDLE;
            Serial.println("[BTN] Long press — full reset");
            ledBlink(5, 100, 50);
        } else if (held >= BTN_DEBOUNCE_MS) {
            // Short press — toggle
            if (s_state == DeviceState::IDLE) {
                enterRecording();
            } else if (s_state == DeviceState::RECORDING) {
                enterIdle();
            }
        }
    }

    lastState = cur;
}

//  SERIAL COMMANDS

/**
 * @brief Dispatch one Serial command byte.
 *  s=start  x=stop  l=list  p=print  d=delete  r=reset  f=fs  i=stats
 */
static void handleSerial() {
    if (!Serial.available()) return;
    const char cmd = static_cast<char>(Serial.read());

    switch (cmd) {
        case 's': enterRecording(); break;

        case 'x': enterIdle(); break;

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
                " | rate=%.1fspm | rest=%s | var=%.4f"
                " | elapsed=%.1fs | clients=%d\n",
                stateName(s_state),
                lapCtr.lapCount(),
                (unsigned long)strokeDet.strokeCount(),
                (unsigned long)lapStrokeCount,
                strokeDet.strokeRateSpm(),
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

//  SETUP


void setup() {
    Serial.begin(115200);
    delay(300);

    pinMode(PIN_LED,    OUTPUT);
    pinMode(PIN_BUTTON, INPUT_PULLUP);
    digitalWrite(PIN_LED, LOW);

    // Check wakeup reason
    esp_sleep_wakeup_cause_t wakeup = esp_sleep_get_wakeup_cause();
    if (wakeup == ESP_SLEEP_WAKEUP_EXT0) {
        Serial.println("[MAIN] Woke from deep sleep via button.");
    }

    Serial.println();
    Serial.println("==========================================");
    Serial.printf( "  SwimTrack FW v%s\n", FW_VERSION);
    Serial.println("  Prompt 9 — Full Integration");
    Serial.println("  Commands: s x l p d r f i");
    Serial.println("==========================================");

    // Run self-test (halts on failure)
    selfTest();

    // Init filters and detectors (IMU + LittleFS already started in selfTest)
    magFilter.reset(1.0f);
    strokeDet.begin();
    lapCtr.begin();

    // Start WiFi AP + REST API
    wifiBegin(&sessMgr, &strokeDet, &lapCtr, &lastSample);

    Serial.printf("[MAIN] Ready. State=%s  Pool=%dm\n",
                  stateName(s_state), poolLengthM);
    Serial.println("[MAIN] Short press BOOT button to start/stop session.");
    Serial.println("[MAIN] Long press  BOOT button (3s) to reset counters.");

    const uint32_t now = millis();
    s_nextSampleMs = s_rateWindowMs = s_lastImuPrintMs = now;
    s_lastClientMs = now;
}

// ============================================================
//  LOOP
// ============================================================

void loop() {
    const uint32_t now = millis();

    // Service WiFi + Serial + Button (every iteration)
    wifiLoop();
    handleSerial();
    handleButton();

    // ---- Track WiFi client for sleep timeout ----
    if (WiFi.softAPgetStationNum() > 0) {
        s_lastClientMs = now;
    }

    // ---- Sleep timeout (IDLE only, no active session) ----
    if (s_state == DeviceState::IDLE &&
        !sessMgr.isActive() &&
        (now - s_lastClientMs) >= SLEEP_TIMEOUT_MS) {
        enterSleep();   // does not return
    }

    // ---- 50 Hz IMU sample gate ----
    if (now >= s_nextSampleMs) {
        s_nextSampleMs += SAMPLE_PERIOD_MS;
        if (now > s_nextSampleMs + SAMPLE_PERIOD_MS)
            s_nextSampleMs = now + SAMPLE_PERIOD_MS;

        if (imu.read(lastSample)) {
            const float filtMag = magFilter.update(accelMagnitude(lastSample));

            // ---- Only run algorithms when RECORDING ----
            if (s_state == DeviceState::RECORDING) {

                // Stroke detection
                if (strokeDet.update(filtMag, now)) {
                    lapStrokeCount++;
                    Serial.printf(
                        "[STROKE] #%lu | Lap:%lu | %.1fspm | %s\n",
                        (unsigned long)strokeDet.strokeCount(),
                        (unsigned long)lapStrokeCount,
                        strokeDet.strokeRateSpm(),
                        strokeTypeName(strokeDet.strokeType()));
                    ledFlash(2);
                }

                // Lap detection
                if (lapCtr.update(lastSample.gz, filtMag,
                                  lapStrokeCount,
                                  strokeDet.strokeRateSpm(), now,
                                  poolLengthM)) {
                    const LapRecord& lr = lapCtr.lastLap();
                    Serial.printf(
                        "[LAP] #%u | %.1fs | Str:%lu | SWOLF:%.1f | %.1fspm | DPS:%.2fm\n",
                        lr.lap_number, lr.duration_s,
                        (unsigned long)lr.stroke_count,
                        lr.swolf, lr.stroke_rate_spm,
                        lr.dps_m_per_stroke);
                    if (sessMgr.isActive()) sessMgr.recordLap(lr);
                    lapStrokeCount = 0;
                    ledFlash(200);
                }

                // Rest events
                if (lapCtr.restJustStarted()) {
                    restStartMs = now;
                    Serial.printf("[REST] Started (var=%.4f g²)\n",
                                  lapCtr.currentVariance());
                }
                if (lapCtr.restJustEnded()) {
                    Serial.printf("[REST] Ended (%.1fs)\n",
                                  lapCtr.lastRestDurationS());
                    if (sessMgr.isActive())
                        sessMgr.recordRest(restStartMs,
                                           lapCtr.lastRestDurationS());
                }

            } else {
                // IDLE — still run lap/variance for /api/live accuracy,
                // but don't count strokes or record laps
                lapCtr.update(lastSample.gz, filtMag, 0,
                              strokeDet.strokeRateSpm(), now,
                              poolLengthM);
            }

            // Throttled debug line
            if (now - s_lastImuPrintMs >= IMU_PRINT_MS) {
                DBG("IMU", "state=%s gz=%.2f filt=%.4f clients=%d heap=%u",
                    stateName(s_state), lastSample.gz, filtMag,
                    (int)WiFi.softAPgetStationNum(),
                    (unsigned)ESP.getFreeHeap());
                s_lastImuPrintMs = now;
            }

            s_sampleCount++;
        } else {
            DBG("MAIN", "WARNING: imu.read() failed");
        }
    }

    // ---- 5 s rate report ----
    if (now - s_rateWindowMs >= RATE_CHECK_MS) {
        const float el = (float)(now - s_rateWindowMs) / 1000.0f;
        DBG("RATE", "%.1fHz | state=%s | clients=%d | heap=%u",
            (float)s_sampleCount / el,
            stateName(s_state),
            (int)WiFi.softAPgetStationNum(),
            (unsigned)ESP.getFreeHeap());
        s_sampleCount  = 0;
        s_rateWindowMs = now;
    }
}