/**
 * @file    main_p6_wifi_server.cpp
 * @brief   SwimTrack — Prompt 6: WiFi SoftAP + REST API.
 *
 *          USE: Rename to main.cpp to flash this step.
 *
 *          What it tests:
 *            - ESP32 SoftAP "SwimTrack" (pass: swim1234) starts at boot
 *            - All REST endpoints available at http://192.168.4.1/
 *            - All Serial commands from Prompt 5 still work
 *            - wifiLoop() called every loop() iteration to service HTTP clients
 *
 *          Test sequence:
 *            1. Flash + open Serial Monitor → see [WIFI] AP started message
 *            2. Connect phone to "SwimTrack" WiFi (pass: swim1234)
 *            3. Open http://192.168.4.1/api/status  → JSON
 *            4. Open http://192.168.4.1/api/live    → real-time IMU values
 *            5. POST http://192.168.4.1/api/session/start  body: {"pool_length_m":25}
 *            6. Simulate strokes/turns
 *            7. POST http://192.168.4.1/api/session/stop
 *            8. GET  http://192.168.4.1/api/sessions → saved session in list
 *
 *          REST endpoints:
 *            GET  /                      HTML placeholder
 *            GET  /api/status            device health
 *            GET  /api/live              real-time IMU + swim metrics
 *            GET  /api/sessions          list of session summaries
 *            GET  /api/sessions/<id>     full session JSON
 *            POST /api/session/start     {pool_length_m:25}
 *            POST /api/session/stop      saves session
 *            POST /api/config            {pool_length_m:N}
 *            DELETE /api/sessions/<id>   delete session file
 *
 *          Required files: config.h  mpu6500.h/.cpp  imu_filters.h/.cpp
 *                          stroke_detector.h/.cpp/_part2.cpp
 *                          lap_counter.h/.cpp/_part2.cpp
 *                          session_manager.h/.cpp/_part2.cpp/_part3.cpp
 *                          wifi_server.h  wifi_server.cpp  wifi_live.cpp
 *                          wifi_api.cpp
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
#include "wifi_server.h"

// ---- Globals (also accessed via extern in wifi_server.h) ----
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
            sessMgr.startSession(poolLengthM); break;
        case 'x':
            if (sessMgr.isActive())
                sessMgr.stopSession(strokeDet.strokeCount());
            else Serial.println("[CMD] No active session."); break;
        case 'l': sessMgr.listSessions(); break;
        case 'p':
            if (sessMgr.lastSavedId()) sessMgr.printSession(sessMgr.lastSavedId());
            else Serial.println("[CMD] No saved session."); break;
        case 'd':
            if (sessMgr.lastSavedId()) sessMgr.deleteSession(sessMgr.lastSavedId());
            else Serial.println("[CMD] No saved session."); break;
        case 'r':
            strokeDet.reset(); lapCtr.reset();
            lapStrokeCount = 0; restStartMs = 0;
            Serial.println("[CMD] Counters reset."); break;
        case 'f': sessMgr.printFSInfo(); break;
        case 'i':
            Serial.printf(
                "[STATS] sess=%s laps=%u strokes=%lu lapStr=%lu"
                " rate=%.1fspm rest=%s var=%.4f elapsed=%.1fs\n",
                sessMgr.isActive()?"ACTIVE":"IDLE",
                lapCtr.lapCount(),
                (unsigned long)strokeDet.strokeCount(),
                (unsigned long)lapStrokeCount,
                strokeDet.strokeRateSpm(),
                lapCtr.isResting()?"YES":"NO",
                lapCtr.currentVariance(),
                lapCtr.currentLapElapsedS()); break;
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
    Serial.println("  SwimTrack — Prompt 6: WiFi REST API");
    Serial.println("  Connect to: SwimTrack / swim1234");
    Serial.println("  Commands: s x l p d r f i");
    Serial.println("==========================================");

    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, LOW);

    if (!imu.begin()) fatalError("imu.begin() failed — check SDA/SCL wiring");
    DBG("MAIN", "IMU OK. WHO_AM_I=0x%02X", imu.whoAmI());

    magFilter.reset(1.0f);
    strokeDet.begin();
    lapCtr.begin();

    if (!sessMgr.begin())
        Serial.println("[MAIN] WARNING: LittleFS failed — sessions disabled");

    // Start WiFi SoftAP + register all REST routes
    wifiBegin(&sessMgr, &strokeDet, &lapCtr, &lastSample);

    DBG("MAIN", "All subsystems ready. Pool=%d m", poolLengthM);

    // Serial Plotter header
    Serial.println("filtered_mag threshold gyroZ_norm variance_x10 resting");

    const uint32_t now = millis();
    s_nextSampleMs = s_rateWindowMs = s_lastImuPrintMs = now;
}

void loop()
{
    const uint32_t now = millis();

    // Service WiFi HTTP clients — must be called every loop iteration
    wifiLoop();
    handleSerial();

    if (now >= s_nextSampleMs) {
        s_nextSampleMs += SAMPLE_PERIOD_MS;
        if (now > s_nextSampleMs + SAMPLE_PERIOD_MS)
            s_nextSampleMs = now + SAMPLE_PERIOD_MS;

        if (imu.read(lastSample)) {
            const float filtMag = magFilter.update(accelMagnitude(lastSample));

            if (strokeDet.update(filtMag, now)) {
                lapStrokeCount++;
                Serial.printf("[STROKE] #%lu | Lap:%lu | %.1fspm | %s\n",
                    (unsigned long)strokeDet.strokeCount(),
                    (unsigned long)lapStrokeCount,
                    strokeDet.strokeRateSpm(),
                    strokeTypeName(strokeDet.strokeType()));
                ledFlash(2);
            }

            if (lapCtr.update(lastSample.gz, filtMag,
                              lapStrokeCount, strokeDet.strokeRateSpm(), now)) {
                const LapRecord& lr = lapCtr.lastLap();
                Serial.printf("[LAP] #%u | %.1fs | Str:%lu | SWOLF:%.1f | %.1fspm\n",
                    lr.lap_number, lr.duration_s,
                    (unsigned long)lr.stroke_count, lr.swolf, lr.stroke_rate_spm);
                if (sessMgr.isActive()) sessMgr.recordLap(lr);
                lapStrokeCount = 0;
                ledFlash(200);
            }

            if (lapCtr.restJustStarted()) {
                restStartMs = now;
                Serial.printf("[REST] Started (var=%.4f g²)\n",
                              lapCtr.currentVariance());
            }
            if (lapCtr.restJustEnded()) {
                Serial.printf("[REST] Ended (%.1f s)\n", lapCtr.lastRestDurationS());
                if (sessMgr.isActive())
                    sessMgr.recordRest(restStartMs, lapCtr.lastRestDurationS());
            }

            Serial.printf("%.4f %.4f %.4f %.4f %.4f\n",
                filtMag, strokeDet.threshold(),
                fabsf(lastSample.gz) / TURN_GYRO_Z_THRESH_DPS,
                lapCtr.currentVariance() * 10.0f,
                lapCtr.isResting() ? 0.3f : 0.0f);

            if (now - s_lastImuPrintMs >= IMU_PRINT_MS) {
                DBG("IMU", "gz=%.2f filt=%.4f sess=%s clients=%d",
                    lastSample.gz, filtMag,
                    sessMgr.isActive()?"ACTIVE":"IDLE",
                    (int)WiFi.softAPgetStationNum());
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
