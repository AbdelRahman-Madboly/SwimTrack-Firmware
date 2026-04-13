/**
 * @file    wifi_api.cpp
 * @brief   SwimTrack REST API — POST, DELETE, and session data handlers.
 *
 *          v2 fix — CRITICAL:
 *            handlePostSessionStart() now calls apiStartRecording() instead of
 *            sessMgr->startSession() directly.  The direct call left s_state=IDLE
 *            so the 50 Hz sample loop skipped all stroke/lap algorithms and
 *            strokes were always 0.  apiStartRecording() / apiStopRecording()
 *            are wrappers in main.cpp that call enterRecording() / enterIdle(),
 *            keeping the state machine consistent whether started by button or API.
 *
 * @author  SwimTrack Firmware Team
 * @date    2026-04
 */

#include <WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "wifi_server.h"

// Accessor functions defined in wifi_server.cpp
WebServer&      wifiServerRef();
SessionManager* wifiSessMgr();
StrokeDetector* wifiStrokeDet();
LapCounter*     wifiLapCtr();

// State machine wrappers defined in main.cpp.
// Must be called instead of sessMgr->start/stopSession() so that s_state
// is updated and the 50 Hz loop activates/deactivates algorithms correctly.
extern void apiStartRecording();
extern void apiStopRecording();

// ============================================================
//  INTERNAL HELPERS
// ============================================================

static void apiSendJson(int code, const String& json)
{
    wifiServerRef().sendHeader("Access-Control-Allow-Origin",  "*");
    wifiServerRef().sendHeader("Access-Control-Allow-Methods", "GET,POST,DELETE,OPTIONS");
    wifiServerRef().send(code, "application/json", json);
}

static uint32_t parseIdFromUri(const String& uri)
{
    int slash = uri.lastIndexOf('/');
    if (slash < 0) return 0;
    String idStr = uri.substring(slash + 1);
    idStr.trim();
    return (uint32_t)idStr.toInt();
}

// ============================================================
//  GET /api/sessions
// ============================================================

void handleGetSessions()
{
    File dir = LittleFS.open(SESSION_DIR);
    if (!dir || !dir.isDirectory()) {
        apiSendJson(200, "[]");
        return;
    }

    JsonDocument doc;
    JsonArray    arr = doc.to<JsonArray>();

    File entry = dir.openNextFile();
    while (entry) {
        if (!entry.isDirectory()) {
            JsonDocument summary;
            if (!deserializeJson(summary, entry)) {
                JsonObject obj        = arr.add<JsonObject>();
                obj["id"]            = summary["id"]            | 0;
                obj["duration_s"]    = summary["duration_s"]    | 0.0f;
                obj["laps"]          = summary["laps"]          | 0;
                obj["total_strokes"] = summary["total_strokes"] | 0;
                obj["pool_m"]        = summary["pool_m"]        | 0;
                obj["total_dist_m"]  = summary["total_dist_m"]  | 0.0f;
                obj["avg_swolf"]     = summary["avg_swolf"]     | 0.0f;
                obj["avg_spm"]       = summary["avg_spm"]       | 0.0f;
                obj["avg_dps"]       = summary["avg_dps"]       | 0.0f;
            }
        }
        entry.close();
        entry = dir.openNextFile();
    }
    dir.close();

    String out;
    serializeJson(doc, out);
    apiSendJson(200, out);
}

// ============================================================
//  GET /api/sessions/<id>
// ============================================================

void handleGetSession()
{
    uint32_t id = parseIdFromUri(wifiServerRef().uri());
    if (id == 0) { apiSendJson(400, "{\"error\":\"invalid id\"}"); return; }

    char path[48];
    snprintf(path, sizeof(path), "%s/%lu.json", SESSION_DIR, (unsigned long)id);

    if (!LittleFS.exists(path)) {
        apiSendJson(404, "{\"error\":\"session not found\"}");
        return;
    }

    File f = LittleFS.open(path, "r");
    if (!f) { apiSendJson(500, "{\"error\":\"open failed\"}"); return; }

    wifiServerRef().sendHeader("Access-Control-Allow-Origin", "*");
    wifiServerRef().streamFile(f, "application/json");
    f.close();
}

// ============================================================
//  POST /api/session/start
// ============================================================

void handlePostSessionStart()
{
    uint8_t pool = poolLengthM;

    if (wifiServerRef().hasArg("plain")) {
        JsonDocument req;
        if (!deserializeJson(req, wifiServerRef().arg("plain"))) {
            if (!req["pool_length_m"].isNull()) {
                pool = (uint8_t)(req["pool_length_m"].as<int>());
            }
        }
    }

    // Set pool length BEFORE apiStartRecording() reads poolLengthM
    poolLengthM = pool;

    // apiStartRecording() calls enterRecording() in main.cpp which:
    //   1. Resets strokeDet, lapCtr, lapStrokeCount
    //   2. Calls sessMgr.startSession(poolLengthM)
    //   3. Sets s_state = RECORDING  <-- activates 50 Hz algorithms
    apiStartRecording();

    DBG("WIFI", "Session started via API | pool=%d m | active=%d",
        pool, (int)wifiSessMgr()->isActive());

    String resp = "{\"ok\":true,\"pool_m\":";
    resp += pool;
    resp += ",\"id\":";
    resp += wifiSessMgr()->isActive() ? String(millis()) : "0";
    resp += "}";
    apiSendJson(200, resp);
}

// ============================================================
//  POST /api/session/stop
// ============================================================

void handlePostSessionStop()
{
    if (!wifiSessMgr()->isActive()) {
        apiSendJson(400, "{\"error\":\"no active session\"}");
        return;
    }

    // apiStopRecording() calls enterIdle() in main.cpp which:
    //   1. Calls sessMgr.stopSession(strokeDet.strokeCount())
    //   2. Sets s_state = IDLE
    apiStopRecording();

    DBG("WIFI", "Session stopped via API | saved_id=%lu",
        (unsigned long)wifiSessMgr()->lastSavedId());

    String resp = "{\"ok\":true,\"saved_id\":";
    resp += wifiSessMgr()->lastSavedId();
    resp += "}";
    apiSendJson(200, resp);
}

// ============================================================
//  POST /api/config
// ============================================================

void handlePostConfig()
{
    if (wifiServerRef().hasArg("plain")) {
        JsonDocument req;
        if (!deserializeJson(req, wifiServerRef().arg("plain"))) {
            if (!req["pool_length_m"].isNull()) {
                poolLengthM = (uint8_t)(req["pool_length_m"].as<int>());
                DBG("WIFI", "Config update: pool_length_m=%d", poolLengthM);
            }
        }
    }

    String resp = "{\"ok\":true,\"pool_m\":";
    resp += poolLengthM;
    resp += "}";
    apiSendJson(200, resp);
}

// ============================================================
//  DELETE /api/sessions/<id>
// ============================================================

void handleDeleteSession()
{
    uint32_t id = parseIdFromUri(wifiServerRef().uri());
    if (id == 0) { apiSendJson(400, "{\"error\":\"invalid id\"}"); return; }

    bool ok = wifiSessMgr()->deleteSession(id);
    if (ok) {
        apiSendJson(200, "{\"ok\":true}");
    } else {
        apiSendJson(404, "{\"error\":\"session not found\"}");
    }
}