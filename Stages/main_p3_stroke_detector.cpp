/**
 * @file    main_p3_stroke_detector.cpp
 * @brief   SwimTrack — Prompt 3: Stroke Detector.
 *
 *          USE: Rename to main.cpp to flash this step.
 *
 *          What it tests:
 *            - Two-state FSM stroke detection on filtered accel magnitude
 *            - Serial Plotter: 3 traces — filtered_mag  threshold  above
 *              (above = 0.5 while arm is above threshold, else 0.0)
 *            - On each stroke: [STROKE] #N | Lap stroke: N | Rate: XX.X spm | Type: FREESTYLE
 *            - LED flashes 2 ms on each stroke
 *            - Serial command 'r' → reset stroke count
 *            - Serial command 'i' → live stats
 *
 *          Pass criteria: manual count matches #N within ±1 over 10 strokes.
 *
 *          Required files: config.h  mpu6500.h/.cpp  imu_filters.h/.cpp
 *                          stroke_detector.h/.cpp/_part2.cpp
 *
 * @author  SwimTrack Firmware Team
 * @date    2025-01-01
 */

#include <Arduino.h>
#include "config.h"
#include "mpu6500.h"
#include "imu_filters.h"
#include "stroke_detector.h"

// ---- Globals ----
MPU6500        imu;
EMAFilter      magFilter(EMA_ALPHA);
StrokeDetector strokeDet;
IMUSample      lastSample;

static uint32_t s_lapStrokeCount = 0;   // strokes in current lap (resets on 'r')
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

static void handleSerial()
{
    if (!Serial.available()) return;
    const char cmd = static_cast<char>(Serial.read());
    switch (cmd) {
        case 'r':
            strokeDet.reset();
            s_lapStrokeCount = 0;
            Serial.println("[CMD] Stroke detector reset. Count = 0.");
            break;
        case 'i':
            Serial.printf(
                "[STATS] strokes=%lu | lapStrokes=%lu | rate=%.1f spm"
                " | type=%s | baseline=%.3f g | thresh=%.3f g\n",
                (unsigned long)strokeDet.strokeCount(),
                (unsigned long)s_lapStrokeCount,
                strokeDet.strokeRateSpm(),
                strokeTypeName(strokeDet.strokeType()),
                strokeDet.baseline(),
                strokeDet.threshold());
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
    Serial.println("=========================================");
    Serial.println("  SwimTrack — Prompt 3: Stroke Detector");
    Serial.println("  Plotter: filtered_mag threshold above");
    Serial.println("  Commands: r=reset  i=stats");
    Serial.println("=========================================");

    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, LOW);

    if (!imu.begin())
        fatalError("imu.begin() failed — check SDA/SCL wiring");
    DBG("MAIN", "IMU OK. WHO_AM_I=0x%02X", imu.whoAmI());

    magFilter.reset(1.0f);
    strokeDet.begin();
    DBG("MAIN", "strokeDet ready. thresh=%.3f g", strokeDet.threshold());

    // Serial Plotter header — must be LAST startup line
    Serial.println("filtered_mag threshold above");

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
            const float filtMag  = magFilter.update(accelMagnitude(lastSample));
            const bool newStroke = strokeDet.update(filtMag, now);

            if (newStroke) {
                s_lapStrokeCount++;
                Serial.printf(
                    "[STROKE] #%lu | Lap stroke: %lu | Rate: %.1f spm | Type: %s\n",
                    (unsigned long)strokeDet.strokeCount(),
                    (unsigned long)s_lapStrokeCount,
                    strokeDet.strokeRateSpm(),
                    strokeTypeName(strokeDet.strokeType()));
                // 2 ms LED flash
                digitalWrite(PIN_LED, HIGH); delay(2); digitalWrite(PIN_LED, LOW);
            }

            // Serial Plotter: filtered_mag  threshold  above
            Serial.printf("%.4f %.4f %.4f\n",
                filtMag,
                strokeDet.threshold(),
                strokeDet.isAboveThreshold() ? 0.5f : 0.0f);

            if (now - s_lastImuPrintMs >= IMU_PRINT_MS) {
                DBG("IMU", "filt=%.4f thr=%.4f baseline=%.4f strokes=%lu",
                    filtMag, strokeDet.threshold(),
                    strokeDet.baseline(),
                    (unsigned long)strokeDet.strokeCount());
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
