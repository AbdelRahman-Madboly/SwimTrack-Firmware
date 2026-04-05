/**
 * @file    main_p5_session_manager.cpp
 * @brief   SwimTrack — Prompt 5: Session Manager + LittleFS Storage.
 *
 *          USE: Rename to main.cpp to flash this step.
 *
 *          What it tests:
 *            - LittleFS mount + /sessions directory creation on first boot
 *            - Session start/stop with JSON serialisation to flash
 *            - Lap and rest records accumulated into session
 *            - All Serial commands from previous prompts plus storage commands
 *
 *          Serial commands:
 *            s = start session (pool = 25 m)
 *            x = stop + save session → prints JSON to Serial
 *            l = list all saved sessions (table)
 *            p = print full JSON of last saved session
 *            d = delete last saved session
 *            r = reset counters only (session stays open)
 *            f = print LittleFS used/total space
 *            i = live stats snapshot
 *
 *          Test sequence:
 *            1. Send 's' → [SESSION] Started | ID: XXXXX | Pool: 25 m
 *            2. Simulate strokes + one turn
 *            3. Send 'x' → [SESSION] Saved + pretty JSON
 *            4. Send 'l' → table of sessions
 *            5. Send 'p' → full JSON
 *            6. Send 'f' → flash usage
 *
 *          Required files: config.h  mpu6500.h/.cpp  imu_filters.h/.cpp
 *                          stroke_detector.h/.cpp/_part2.cpp
 *                          lap_counter.h/.cpp/_part2.cpp
 *                          session_manager.h/.cpp/_part2.cpp/_part3.cpp
 *
 * @author  SwimTrack Firmware Team
 * @date    2025-01-01
 */

#include <Arduino.h>
#include "config.h"
#include "mpu6500.h"
#include "imu_filters.h"
#include "stroke_detector.h"
#include "lap_counter.h"
#include "session_manager.h"

// ---- Globals ----
MPU6500        imu;
EMAFilter      magFilter(EMA_ALPHA);
StrokeDetector strokeDet;
LapCounter     lapCtr;
SessionManager sessMgr;
IMUSample      lastSample;

uint32_t lapStrokeCount = 0;
uint8_t  poolLengthM    = DEFAULT_POOL_LENGTH_M;
uint32_t restStartMs    = 0;

static uint32_t s_nextSampleMs   = 0;
static uint32_t s_sampleCount    = 0;
static uint32_t s_rateWindowMs   = 0;
static uint32_t s_lastImuPrintMs = 0;
static const uint32_t RATE_CHECK_MS = 5000UL;
static const uint32_t IMU_PRINT_MS  = 5000UL;

static void fatalError(const char* msg)
{
    Serial.printf("[FATAL] %s\n", msg);
    pinMode(PIN_LED, OUTPUT);
    while (true) { digitalWrite(PIN_LED,HIGH); delay(100);
                   digitalWrite(PIN_LED,LOW);  delay(100); }
}

static void ledFlash(uint32_t ms)
{ digitalWrite(PIN_LED,HIGH); delay(ms); digitalWrite(PIN_LED,LOW); }

static void handleSerial()
{
    if (!Serial.available()) return;
    const char cmd = static_cast<char>(Serial.read());
    switch (cmd) {
        case 's':
            strokeDet.reset(); lapCtr.reset();
            lapStrokeCount = 0; restStartMs = 0;
            sessMgr.startSession(poolLengthM);
            break;
        case 'x':
            if (sessMgr.isActive())
                sessMgr.stopSession(strokeDet.strokeCount());
            else Serial.println("[CMD] No active session.");
            break;
        case 'l': sessMgr.listSessions(); break;
        case 'p':
            if (sessMgr.lastSavedId())
                sessMgr.printSession(sessMgr.lastSavedId());
            else Serial.println("[CMD] No saved session this boot.");
            break;
        case 'd':
            if (sessMgr.lastSavedId())
                sessMgr.deleteSession(sessMgr.lastSavedId());
            else Serial.println("[CMD] No saved session to delete.");
            break;
        case 'r':
            strokeDet.reset(); lapCtr.reset();
            lapStrokeCount = 0; restStartMs = 0;
            Serial.println("[CMD] Counters reset.");
            break;
        case 'f': sessMgr.printFSInfo(); break;
        case 'i':
            Serial.printf(
                "[STATS] sess=%s | laps=%u | strokes=%lu | lapStr=%lu"
                " | rate=%.1f spm | rest=%s | var=%.4f | elapsed=%.1f s\n",
                sessMgr.isActive() ? "ACTIVE" : "IDLE",
                lapCtr.lapCount(),
                (unsigned long)strokeDet.strokeCount(),
                (unsigned long)lapStrokeCount,
                strokeDet.strokeRateSpm(),
                lapCtr.isResting() ? "YES" : "NO",
                lapCtr.currentVariance(),
                lapCtr.currentLapElapsedS());
            break;
        default:
            if (cmd >= 0x20)
                Serial.printf("[CMD] Unknown '%c'. Known: s x l p d r f i\n", cmd);
            break;
    }
}

void setup()
{
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println("==========================================");
    Serial.println("  SwimTrack — Prompt 5: Session Manager");
    Serial.println("  Commands: s x l p d r f i");
    Serial.println("==========================================");

    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, LOW);

    if (!imu.begin())
        fatalError("imu.begin() failed — check SDA/SCL wiring");
    DBG("MAIN", "IMU OK. WHO_AM_I=0x%02X", imu.whoAmI());

    magFilter.reset(1.0f);
    strokeDet.begin();
    lapCtr.begin();

    if (!sessMgr.begin())
        Serial.println("[MAIN] WARNING: LittleFS failed — sessions disabled");

    DBG("MAIN", "All subsystems ready. Pool=%d m", poolLengthM);
    Serial.println("[MAIN] Send 's' to start, 'x' to stop, 'l' to list.");

    // Serial Plotter header
    Serial.println("filtered_mag threshold gyroZ_norm variance_x10 resting");

    const uint32_t now = millis();
    s_nextSampleMs = s_rateWindowMs = s_lastImuPrintMs = now;
}

void loop()
{
    const uint32_t now = millis();
    handleSerial();

    if (now >= s_nextSampleMs) {
        s_nextSampleMs += SAMPLE_PERIOD_MS;
        if (now > s_nextSampleMs + SAMPLE_PERIOD_MS)
            s_nextSampleMs = now + SAMPLE_PERIOD_MS;

        if (imu.read(lastSample)) {
            const float filtMag = magFilter.update(accelMagnitude(lastSample));

            // ---- Stroke ----
            if (strokeDet.update(filtMag, now)) {
                lapStrokeCount++;
                Serial.printf("[STROKE] #%lu | Lap: %lu | %.1f spm | %s\n",
                    (unsigned long)strokeDet.strokeCount(),
                    (unsigned long)lapStrokeCount,
                    strokeDet.strokeRateSpm(),
                    strokeTypeName(strokeDet.strokeType()));
                ledFlash(2);
            }

            // ---- Lap ----
            if (lapCtr.update(lastSample.gz, filtMag,
                              lapStrokeCount, strokeDet.strokeRateSpm(), now)) {
                const LapRecord& lr = lapCtr.lastLap();
                Serial.printf("[LAP] #%u | %.1fs | Str:%lu | SWOLF:%.1f | %.1f spm\n",
                    lr.lap_number, lr.duration_s,
                    (unsigned long)lr.stroke_count, lr.swolf, lr.stroke_rate_spm);
                if (sessMgr.isActive()) sessMgr.recordLap(lr);
                lapStrokeCount = 0;
                ledFlash(200);
            }

            // ---- Rest ----
            if (lapCtr.restJustStarted()) {
                restStartMs = now;
                Serial.printf("[REST] Started (var=%.4f g²)\n",
                              lapCtr.currentVariance());
            }
            if (lapCtr.restJustEnded()) {
                Serial.printf("[REST] Ended (%.1f s)\n",
                              lapCtr.lastRestDurationS());
                if (sessMgr.isActive())
                    sessMgr.recordRest(restStartMs, lapCtr.lastRestDurationS());
            }

            // ---- Serial Plotter: 5 traces ----
            Serial.printf("%.4f %.4f %.4f %.4f %.4f\n",
                filtMag, strokeDet.threshold(),
                fabsf(lastSample.gz) / TURN_GYRO_Z_THRESH_DPS,
                lapCtr.currentVariance() * 10.0f,
                lapCtr.isResting() ? 0.3f : 0.0f);

            if (now - s_lastImuPrintMs >= IMU_PRINT_MS) {
                DBG("IMU", "gz=%.2f filt=%.4f sess=%s",
                    lastSample.gz, filtMag,
                    sessMgr.isActive() ? "ACTIVE" : "IDLE");
                s_lastImuPrintMs = now;
            }
            s_sampleCount++;
        } else {
            DBG("MAIN", "WARNING: imu.read() failed");
        }
    }

    if (now - s_rateWindowMs >= RATE_CHECK_MS) {
        const float el = (float)(now - s_rateWindowMs) / 1000.0f;
        DBG("RATE", "%lu samples / %.2fs = %.2fHz",
            s_sampleCount, el, (float)s_sampleCount / el);
        s_sampleCount  = 0;
        s_rateWindowMs = now;
    }
}
