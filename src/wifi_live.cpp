/**
 * @file    wifi_live.cpp
 * @brief   GET /api/live handler — real-time IMU and swim metrics snapshot.
 *
 *          Split from wifi_server.cpp to stay under 200 lines.
 *
 *          v2 changes:
 *            - Added real battery ADC read (PIN_BATTERY_ADC / GPIO1).
 *              Reads analogRead(), converts to mV via voltage divider, then
 *              maps to percentage between BATT_EMPTY_MV and BATT_FULL_MV.
 *              Adds "batt_mv" (int) and "batt_pct" (int) to the JSON response.
 *              The Flutter app's LiveData.fromJson() reads batt_pct for the
 *              battery indicator in the recording view.
 *
 * @author  SwimTrack Firmware Team
 * @date    2026-04
 */

#include <WebServer.h>
#include <ArduinoJson.h>
#include "wifi_server.h"

// External globals defined in main.cpp
extern uint32_t lapStrokeCount;

// Accessor functions defined at the bottom of wifi_server.cpp
WebServer&      wifiServerRef();
StrokeDetector* wifiStrokeDet();
LapCounter*     wifiLapCtr();
SessionManager* wifiSessMgr();
IMUSample*      wifiSamplePtr();

// ============================================================
//  INTERNAL HELPER
// ============================================================

static void liveJson(int code, const String& json)
{
    wifiServerRef().sendHeader("Access-Control-Allow-Origin", "*");
    wifiServerRef().send(code, "application/json", json);
}

// ============================================================
//  ROUTE HANDLER — GET /api/live
// ============================================================

/**
 * @brief Assemble a real-time JSON snapshot and send it to the client.
 *
 *        Called by wifi_server.cpp's route table via forward declaration.
 *        Reads the most recent 50 Hz IMUSample and the current stroke / lap
 *        metrics without touching the private state of wifi_server.cpp.
 *
 *        Typical polling rate from the mobile app: 1 Hz.
 */
void handleLive()
{
    JsonDocument doc;   // ArduinoJson v7 — replaces StaticJsonDocument<512>

    // ── IMU values from last 50 Hz sample ────────────────────
    IMUSample* sp = wifiSamplePtr();
    if (sp) {
        doc["ax"]     = serialized(String(sp->ax,     3));
        doc["ay"]     = serialized(String(sp->ay,     3));
        doc["az"]     = serialized(String(sp->az,     3));
        doc["gx"]     = serialized(String(sp->gx,     2));
        doc["gy"]     = serialized(String(sp->gy,     2));
        doc["gz"]     = serialized(String(sp->gz,     2));
        doc["temp_c"] = serialized(String(sp->temp_c, 1));
    }

    // ── Stroke metrics ────────────────────────────────────────
    StrokeDetector* sd = wifiStrokeDet();
    if (sd) {
        doc["strokes"]     = sd->strokeCount();
        doc["rate_spm"]    = serialized(String(sd->strokeRateSpm(), 1));
        doc["stroke_type"] = strokeTypeName(sd->strokeType());
        doc["lap_strokes"] = lapStrokeCount;
    }

    // ── Lap + rest metrics ────────────────────────────────────
    LapCounter* lc = wifiLapCtr();
    if (lc) {
        doc["laps"]          = lc->lapCount();
        doc["resting"]       = lc->isResting();
        doc["lap_elapsed_s"] = serialized(String(lc->currentLapElapsedS(), 1));
        doc["variance"]      = serialized(String(lc->currentVariance(),    4));

        // Estimated current-lap SWOLF = lap strokes + elapsed seconds
        float estSwolf = (float)lapStrokeCount + lc->currentLapElapsedS();
        doc["swolf_est"] = serialized(String(estSwolf, 1));

        // DPS — last completed lap distance per stroke
        doc["lap_dps"] = serialized(String(lc->lastLap().dps_m_per_stroke, 2));
    }

    // ── Session state ─────────────────────────────────────────
    SessionManager* sm = wifiSessMgr();
    if (sm) {
        doc["session_active"] = sm->isActive();
        doc["session_laps"]   = sm->currentLapCount();
    }

    // ── Battery voltage — read ADC, convert to mV, compute percentage ────────
    // Hardware: 100k+100k voltage divider from LIPO+ to PIN_BATTERY_ADC (GPIO1).
    // ESP32-S2 does not disable ADC during WiFi AP (unlike standard ESP32).
    {
        uint32_t rawAdc = analogRead(PIN_BATTERY_ADC);
        float vBatt = ((float)rawAdc / (float)ADC_MAX_COUNT)
                      * (float)ADC_VREF_MV / BATT_DIVIDER_RATIO;

        uint8_t battPct = 0;
        if      (vBatt >= (float)BATT_FULL_MV)  battPct = 100;
        else if (vBatt <= (float)BATT_EMPTY_MV) battPct = 0;
        else battPct = (uint8_t)(100.0f * (vBatt - (float)BATT_EMPTY_MV)
                                         / ((float)BATT_FULL_MV - (float)BATT_EMPTY_MV));

        doc["batt_mv"]  = (int)vBatt;
        doc["batt_pct"] = battPct;

        DBG("LIVE", "Battery: raw=%lu vBatt=%.0fmV pct=%u%%",
            (unsigned long)rawAdc, vBatt, (unsigned)battPct);
    }

    String out;
    serializeJson(doc, out);
    liveJson(200, out);
}