/**
 * @file    main_p2_imu_filters.cpp
 * @brief   SwimTrack — Prompt 2: IMU Filters + Serial Plotter.
 *
 *          USE: Rename to main.cpp to flash this step.
 *
 *          What it tests:
 *            - EMA filter on accel magnitude (alpha = 0.3)
 *            - Serial Plotter: 2 traces — raw_mag  filtered_mag
 *            - Board flat → both traces ≈ 1.0 g
 *            - Wave board → raw spikes; filtered follows with slight lag
 *            - [RATE] line every 5 s confirms 50 Hz sample rate
 *
 *          Required files: config.h  mpu6500.h/.cpp  imu_filters.h/.cpp
 *
 * @author  SwimTrack Firmware Team
 * @date    2025-01-01
 */

#include <Arduino.h>
#include "config.h"
#include "mpu6500.h"
#include "imu_filters.h"

// ---- Globals ----
MPU6500   imu;
EMAFilter magFilter(EMA_ALPHA);
IMUSample lastSample;

static uint32_t s_nextSampleMs   = 0;
static uint32_t s_sampleCount    = 0;
static uint32_t s_rateWindowMs   = 0;
static uint32_t s_lastImuPrintMs = 0;
static const uint32_t RATE_CHECK_MS = 5000UL;
static const uint32_t IMU_PRINT_MS  = 5000UL;

static void fatalError(const char* msg)
{
    Serial.printf("[FATAL] %s\n", msg);
    Serial.println("[FATAL] Halting. Fix wiring and reset.");
    pinMode(PIN_LED, OUTPUT);
    while (true) { digitalWrite(PIN_LED,HIGH); delay(100);
                   digitalWrite(PIN_LED,LOW);  delay(100); }
}

void setup()
{
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println("========================================");
    Serial.println("  SwimTrack — Prompt 2: IMU Filters");
    Serial.println("  Serial Plotter: raw_mag  filtered_mag");
    Serial.println("========================================");

    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, LOW);

    if (!imu.begin())
        fatalError("imu.begin() failed — check SDA/SCL wiring");
    DBG("MAIN", "IMU OK. WHO_AM_I=0x%02X", imu.whoAmI());

    // Seed filter to 1.0 g — prevents ramp-up from 0 in plotter
    magFilter.reset(1.0f);
    DBG("MAIN", "magFilter seeded 1.0 g (alpha=%.2f)", magFilter.alpha());

    // Serial Plotter column header — must be LAST startup line
    Serial.println("raw_mag filtered_mag");

    const uint32_t now = millis();
    s_nextSampleMs = s_rateWindowMs = s_lastImuPrintMs = now;
}

void loop()
{
    const uint32_t now = millis();

    if (now >= s_nextSampleMs) {
        s_nextSampleMs += SAMPLE_PERIOD_MS;
        if (now > s_nextSampleMs + SAMPLE_PERIOD_MS)
            s_nextSampleMs = now + SAMPLE_PERIOD_MS;

        if (imu.read(lastSample)) {
            const float rawMag  = accelMagnitude(lastSample);
            const float filtMag = magFilter.update(rawMag);

            // Serial Plotter — 2 space-separated values
            Serial.printf("%.4f %.4f\n", rawMag, filtMag);

            // Throttled full IMU debug every 5 s
            if (now - s_lastImuPrintMs >= IMU_PRINT_MS) {
                DBG("IMU",
                    "ax=%6.3f ay=%6.3f az=%6.3f | "
                    "gx=%7.2f gy=%7.2f gz=%7.2f | "
                    "T=%5.1f | raw=%.4f filt=%.4f",
                    lastSample.ax, lastSample.ay, lastSample.az,
                    lastSample.gx, lastSample.gy, lastSample.gz,
                    lastSample.temp_c, rawMag, filtMag);
                s_lastImuPrintMs = now;
            }
            s_sampleCount++;
        } else {
            DBG("MAIN", "WARNING: imu.read() failed");
        }
    }

    if (now - s_rateWindowMs >= RATE_CHECK_MS) {
        const float el = (float)(now - s_rateWindowMs) / 1000.0f;
        DBG("RATE", "%lu samples in %.2f s → %.2f Hz (target %d Hz)",
            s_sampleCount, el, (float)s_sampleCount / el, SAMPLE_RATE_HZ);
        s_sampleCount  = 0;
        s_rateWindowMs = now;
    }
}
