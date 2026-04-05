/**
 * @file    wifi_api.cpp
 * @brief   SwimTrack REST API — POST, DELETE, and session data handlers.
 *
 *          Continuation of wifi_server.cpp.  Split to stay under 200 lines.
 *
 *          Contains:
 *            - handleGetSessions()      GET  /api/sessions
 *            - handleGetSession()       GET  /api/sessions/<id>
 *            - handlePostSessionStart() POST /api/session/start
 *            - handlePostSessionStop()  POST /api/session/stop
 *            - handlePostConfig()       POST /api/config
 *            - handleDeleteSession()    DELETE /api/sessions/<id>
 *
 *          ArduinoJson v7: StaticJsonDocument / DynamicJsonDocument
 *          replaced throughout with JsonDocument.
 *
 * @author  SwimTrack Firmware Team
 * @date    2025-01-01
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

// ============================================================
//  INTERNAL HELPERS
// ============================================================

/**
 * @brief Re-usable JSON sender with CORS headers.
 * @param code  HTTP status code.
 * @param json  Response body.
 */
static void apiSendJson(int code, const String& json)
{
    wifiServerRef().sendHeader("Access-Control-Allow-Origin",  "*");
    wifiServerRef().sendHeader("Access-Control-Allow-Methods", "GET,POST,DELETE,OPTIONS");
    wifiServerRef().send(code, "application/json", json);
}

/**
 * @brief Extract the session ID from a URI like /api/sessions/12345.
 * @param uri  Full request URI string.
 * @return uint32_t  Parsed ID, or 0 on failure.
 */
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

/**
 * @brief Return a JSON array of session summaries from LittleFS.
 *
 *        Opens each .json file in /sessions and reads only the top-level
 *        scalar fields (id, duration_s, laps, total_strokes, pool_m,
 *        total_dist_m, avg_swolf) without loading per-lap arrays.
 */
void handleGetSessions()
{
    File dir = LittleFS.open(SESSION_DIR);
    if (!dir || !dir.isDirectory()) {
        apiSendJson(200, "[]");
        return;
    }

    // ArduinoJson v7: JsonDocument (replaces DynamicJsonDocument)
    JsonDocument doc;
    JsonArray    arr = doc.to<JsonArray>();

    File entry = dir.openNextFile();
    while (entry) {
        if (!entry.isDirectory()) {
            JsonDocument summary;   // replaces StaticJsonDocument<512>
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

/**
 * @brief Stream the full JSON of one session file to the HTTP client.
 */
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

/**
 * @brief Start a new session, optionally with a pool length in the body.
 *
 *        Body (optional): {"pool_length_m": 25}
 *        Resets stroke and lap counters before starting.
 */
void handlePostSessionStart()
{
    uint8_t pool = poolLengthM;   // default from extern global

    if (wifiServerRef().hasArg("plain")) {
        JsonDocument req;   // replaces StaticJsonDocument<128>
        if (!deserializeJson(req, wifiServerRef().arg("plain"))) {
            if (!req["pool_length_m"].isNull()) {
                pool = (uint8_t)(req["pool_length_m"].as<int>());
                poolLengthM = pool;
            }
        }
    }

    wifiStrokeDet()->reset();
    wifiLapCtr()->reset();
    lapStrokeCount = 0;
    wifiSessMgr()->startSession(pool);

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

/**
 * @brief Stop the active session and save to LittleFS.
 */
void handlePostSessionStop()
{
    if (!wifiSessMgr()->isActive()) {
        apiSendJson(400, "{\"error\":\"no active session\"}");
        return;
    }

    uint32_t totalStrokes = wifiStrokeDet()
                            ? wifiStrokeDet()->strokeCount() : 0;
    wifiSessMgr()->stopSession(totalStrokes);

    String resp = "{\"ok\":true,\"saved_id\":";
    resp += wifiSessMgr()->lastSavedId();
    resp += "}";
    apiSendJson(200, resp);
}

// ============================================================
//  POST /api/config
// ============================================================

/**
 * @brief Update runtime config values.
 *
 *        Supported keys: pool_length_m
 */
void handlePostConfig()
{
    if (wifiServerRef().hasArg("plain")) {
        JsonDocument req;   // replaces StaticJsonDocument<256>
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

/**
 * @brief Delete one session file from LittleFS.
 */
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