/**
 * @file    main.cpp
 * @brief   SwimTrack firmware entry point — Prompt 1 (foundation / IMU test).
 *
 *          Initialises the MPU-6050 and reads samples at 50 Hz using a
 *          non-blocking millis() loop.  Prints formatted accel, gyro, and
 *          temperature to Serial at 115200 baud.
 *
 *          Every 5 seconds the measured sample rate is calculated and printed
 *          so you can verify timing accuracy without an oscilloscope.
 *
 *          If imu.begin() fails (WHO_AM_I mismatch or I2C error) the LED on
 *          GPIO2 blinks rapidly and execution halts — fix wiring first.
 *
 *          Global IMUSample lastSample is kept up-to-date each cycle so that
 *          later modules (WiFi server, lap counter) can read it without
 *          re-querying the sensor.
 *
 * @author  SwimTrack Firmware Team
 * @date    2025-01-01
 */

#include <Arduino.h>
#include "config.h"
#include "mpu6500.h"

// ============================================================
//  GLOBALS  (extern-accessible for later modules)
// ============================================================

MPU6500    imu;                          ///< IMU driver instance
IMUSample  lastSample;                   ///< Most-recent sample — updated at 50 Hz

// ============================================================
//  PRIVATE STATICS
// ============================================================

static uint32_t s_nextSampleMs   = 0;   ///< Timestamp of next scheduled read
static uint32_t s_sampleCount    = 0;   ///< Samples collected since last rate check
static uint32_t s_rateWindowMs   = 0;   ///< millis() at start of rate-check window

// Rate check window: 5 seconds
static const uint32_t RATE_CHECK_INTERVAL_MS = 5000;

// ============================================================
//  HELPER — fatal error blink
// ============================================================

/**
 * @brief Blink the built-in LED rapidly and halt.
 *
 *  Called when imu.begin() fails.  Halts execution permanently so the
 *  failure is visible without a Serial connection.
 *
 * @param msg  Short error string printed once before the blink loop.
 */
static void fatalError(const char* msg)
{
    Serial.printf("[FATAL] %s\n", msg);
    Serial.println("[FATAL] Halting. Fix wiring and reset.");
    pinMode(PIN_LED, OUTPUT);
    while (true) {
        digitalWrite(PIN_LED, HIGH); delay(100);
        digitalWrite(PIN_LED, LOW);  delay(100);
    }
}

// ============================================================
//  SETUP
// ============================================================

/**
 * @brief Arduino setup — runs once after power-on or reset.
 *
 *  1. Initialises Serial at 115200 baud.
 *  2. Configures the built-in LED pin.
 *  3. Calls imu.begin(); halts on failure.
 *  4. Seeds the sample-rate window and first-sample timestamp.
 */
void setup()
{
    Serial.begin(115200);
    delay(200); // Let the USB-serial bridge enumerate on the host
    Serial.println();
    Serial.println("========================================");
    Serial.println("  SwimTrack Firmware  —  Prompt 1");
    Serial.println("  Target: ESP32 dev + MPU-6050 (0x68)");
    Serial.println("========================================");

    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, LOW);

    DBG("MAIN", "Calling imu.begin()...");
    if (!imu.begin()) {
        fatalError("imu.begin() failed — check SDA/SCL wiring and I2C address");
    }

    DBG("MAIN", "IMU OK. WHO_AM_I=0x%02X", imu.whoAmI());
    Serial.println("[MAIN] Streaming IMU data at 50 Hz. WHO_AM_I check passed.");

    // Seed timing variables
    s_nextSampleMs  = millis();
    s_rateWindowMs  = millis();
    s_sampleCount   = 0;
}

// ============================================================
//  LOOP
// ============================================================

/**
 * @brief Arduino loop — runs repeatedly.
 *
 *  Non-blocking 50 Hz IMU read using millis().
 *  On each sample:
 *    - Reads IMU into lastSample.
 *    - Prints formatted line to Serial.
 *    - Increments sample counter.
 *  Every RATE_CHECK_INTERVAL_MS (5 s):
 *    - Calculates and prints measured sample rate.
 */
void loop()
{
    const uint32_t now = millis();

    // ---- 50 Hz sample gate ----
    if (now >= s_nextSampleMs) {
        s_nextSampleMs += SAMPLE_PERIOD_MS;

        // Guard against timer drift building up if loop() stalls
        if (now > s_nextSampleMs + SAMPLE_PERIOD_MS) {
            s_nextSampleMs = now + SAMPLE_PERIOD_MS;
        }

        if (imu.read(lastSample)) {
            // ---- Print IMU line ----
            Serial.printf(
                "[IMU] ax=%6.3f ay=%6.3f az=%6.3f | "
                "gx=%7.2f gy=%7.2f gz=%7.2f | "
                "T=%5.1f\n",
                lastSample.ax, lastSample.ay, lastSample.az,
                lastSample.gx, lastSample.gy, lastSample.gz,
                lastSample.temp_c
            );

            s_sampleCount++;
        } else {
            DBG("MAIN", "WARNING: imu.read() failed");
        }
    }

    // ---- 5-second rate report ----
    if (now - s_rateWindowMs >= RATE_CHECK_INTERVAL_MS) {
        float elapsed_s = static_cast<float>(now - s_rateWindowMs) / 1000.0f;
        float measuredHz = static_cast<float>(s_sampleCount) / elapsed_s;
        Serial.printf(
            "[RATE] %lu samples in %.2f s → %.2f Hz (target %d Hz)\n",
            s_sampleCount, elapsed_s, measuredHz, SAMPLE_RATE_HZ
        );
        // Reset window
        s_sampleCount  = 0;
        s_rateWindowMs = now;
    }
}
