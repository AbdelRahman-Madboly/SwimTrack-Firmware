/**
 * @file    main_p4_lap_counter.cpp
 * @brief   SwimTrack — Prompt 4: Lap Counter + Rest Detection.
 *
 *          USE: Rename to main.cpp to flash this step.
 *
 *          What it tests:
 *            - Turn FSM: IDLE → SPIKE → GLIDE_WAIT → LAP CONFIRMED
 *            - Rest detection: hold still 5 s → [REST] Started / Ended
 *            - Serial Plotter: 5 traces —
 *                filtered_mag  threshold  gyroZ_norm  variance_x10  resting
 *            - [LAP] #N | Time: XX.Xs | Strokes: XX | SWOLF: XX.X | Rate: XX.X spm
 *            - LED 200 ms flash on lap, 2 ms flash on stroke
 *            - Serial commands: r=full reset  i=live stats
 *
 *          Simulate turn: rotate board briskly around Z-axis for ~300 ms, then hold still.
 *          Simulate rest: set board flat on desk for 5+ seconds.
 *          NOTE: LAP_MIN_DURATION_MS = 5000 in config.h for dev testing.
 *
 *          Required files: config.h  mpu6500.h/.cpp  imu_filters.h/.cpp
 *                          stroke_detector.h/.cpp/_part2.cpp
 *                          lap_counter.h  lap_counter.cpp  lap_counter_part2.cpp
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

// ---- Globals ----
MPU6500        imu;
EMAFilter      magFilter(EMA_ALPHA);
StrokeDetector strokeDet;
LapCounter     lapCtr;
IMUSample      lastSample;

uint32_t lapStrokeCount = 0;
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
        case 'r':
            strokeDet.reset(); lapCtr.reset();
            lapStrokeCount = 0; restStartMs = 0;
            Serial.println("[CMD] Full reset: strokes=0, laps=0.");
            break;
        case 'i':
            Serial.printf(
                "[STATS] laps=%u | strokes=%lu | lapStrokes=%lu"
                " | rate=%.1f spm | resting=%s"
                " | var=%.4f g² | elapsed=%.1f s\n",
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
                Serial.printf("[CMD] Unknown '%c'. Known: r i\n", cmd);
            break;
    }
}

void setup()
{
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println("==========================================");
    Serial.println("  SwimTrack — Prompt 4: Lap Counter");
    Serial.println("  Plotter: filtMag thr gyroZ_n var10 rest");
    Serial.println("  Commands: r=reset  i=stats");
    Serial.println("==========================================");

    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, LOW);

    if (!imu.begin())
        fatalError("imu.begin() failed — check SDA/SCL wiring");
    DBG("MAIN", "IMU OK. WHO_AM_I=0x%02X", imu.whoAmI());

    magFilter.reset(1.0f);
    strokeDet.begin();
    lapCtr.begin();
    DBG("MAIN", "lapCtr ready. min-lap=%d ms", (int)LAP_MIN_DURATION_MS);

    // Serial Plotter header — must be LAST startup line
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

            // ---- Stroke detection ----
            if (strokeDet.update(filtMag, now)) {
                lapStrokeCount++;
                Serial.printf(
                    "[STROKE] #%lu | Lap stroke: %lu | Rate: %.1f spm | Type: %s\n",
                    (unsigned long)strokeDet.strokeCount(),
                    (unsigned long)lapStrokeCount,
                    strokeDet.strokeRateSpm(),
                    strokeTypeName(strokeDet.strokeType()));
                ledFlash(2);
            }

            // ---- Lap detection ----
            if (lapCtr.update(lastSample.gz, filtMag,
                              lapStrokeCount, strokeDet.strokeRateSpm(), now)) {
                const LapRecord& lr = lapCtr.lastLap();
                Serial.printf(
                    "[LAP] #%u | Time: %.1fs | Strokes: %lu"
                    " | SWOLF: %.1f | Rate: %.1f spm\n",
                    lr.lap_number, lr.duration_s,
                    (unsigned long)lr.stroke_count,
                    lr.swolf, lr.stroke_rate_spm);
                lapStrokeCount = 0;
                ledFlash(200);
            }

            // ---- Rest events ----
            if (lapCtr.restJustStarted()) {
                restStartMs = now;
                Serial.printf("[REST] Started (variance=%.4f g²)\n",
                              lapCtr.currentVariance());
            }
            if (lapCtr.restJustEnded()) {
                Serial.printf("[REST] Ended (%.1f s)\n",
                              lapCtr.lastRestDurationS());
            }

            // ---- Serial Plotter: 5 traces ----
            Serial.printf("%.4f %.4f %.4f %.4f %.4f\n",
                filtMag,
                strokeDet.threshold(),
                fabsf(lastSample.gz) / TURN_GYRO_Z_THRESH_DPS,
                lapCtr.currentVariance() * 10.0f,
                lapCtr.isResting() ? 0.3f : 0.0f);

            if (now - s_lastImuPrintMs >= IMU_PRINT_MS) {
                DBG("IMU", "gz=%.2f filt=%.4f var=%.4f laps=%u",
                    lastSample.gz, filtMag,
                    lapCtr.currentVariance(), lapCtr.lapCount());
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
