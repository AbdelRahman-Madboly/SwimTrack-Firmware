/**
 * @file    wifi_live.cpp
 * @brief   GET /api/live handler — real-time IMU and swim metrics snapshot.
 *
 *          Split from wifi_server.cpp to stay under 200 lines.
 *
 *          Fixes applied vs. original:
 *            - Use wifiStrokeDet() / wifiLapCtr() / wifiSessMgr() accessors
 *              instead of the private s_sd / s_lc / s_sm symbols (which are
 *              static inside wifi_server.cpp and therefore invisible here).
 *            - StaticJsonDocument<512> → JsonDocument  (ArduinoJson v7).
 *            - Removed the two orphaned anonymous blocks at the bottom of the
 *              original file (handleOptions body and handleNotFound body);
 *              those functions now live in wifi_server.cpp where they belong.
 *            - handleLive() is now a proper non-static void function so that
 *              wifi_server.cpp can forward-declare and register it.
 *
 * @author  SwimTrack Firmware Team
 * @date    2025-01-01
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
 *        Typical polling rate from the mobile app: 1–2 Hz.
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

        // DPS — last completed lap and running session average
        doc["lap_dps"] = serialized(String(lc->lastLap().dps_m_per_stroke, 2));
    }

    // ── Session state ─────────────────────────────────────────
    SessionManager* sm = wifiSessMgr();
    if (sm) {
        doc["session_active"] = sm->isActive();
        doc["session_laps"]   = sm->currentLapCount();
    }

    String out;
    serializeJson(doc, out);
    liveJson(200, out);
}