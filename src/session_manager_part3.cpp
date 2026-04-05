/**
 * @file    session_manager_part3.cpp
 * @brief   SessionManager private helpers: _buildPath() and _buildJson().
 *
 *          Updated for ArduinoJson v7:
 *            - DynamicJsonDocument  → JsonDocument
 *            - createNestedArray()  → doc["key"].to<JsonArray>()
 *            - createNestedObject() → arr.add<JsonObject>()
 *
 * @author  SwimTrack Firmware Team
 * @date    2025-01-01
 */

#include "session_manager.h"

/**
 * @brief Build the full LittleFS path for a session file.
 *        Result: "/sessions/1234567890.json"
 * @param session_id  Numeric session ID.
 * @param buf         Output buffer (>= 40 bytes).
 * @param bufLen      Size of buf.
 */
void SessionManager::_buildPath(uint32_t session_id, char* buf, size_t bufLen) const
{
    snprintf(buf, bufLen, "%s/%lu.json", SESSION_DIR, (unsigned long)session_id);
}

/**
 * @brief Populate a JsonDocument with all session data and derived metrics.
 *
 *        Computed fields:
 *          duration_s   = (end_ms - start_ms) / 1000
 *          total_dist_m = lap_count * pool_length_m
 *          avg_swolf    = mean of all lap SWOLF values
 *          avg_spm      = mean of all lap stroke rates
 *
 * @param totalStrokes  Stroke count from StrokeDetector at stop time.
 * @param doc           JsonDocument to populate (ArduinoJson v7).
 */
void SessionManager::_buildJson(uint32_t totalStrokes, JsonDocument& doc) const
{
    float durS       = (float)(_session.end_ms - _session.start_ms) / 1000.0f;
    float totalDistM = (float)(_session.lap_count * _session.pool_length_m);

    float sumSwolf = 0.0f, sumSpm = 0.0f, sumDps = 0.0f;
    uint16_t dpsCount = 0;
    for (uint16_t i = 0; i < _session.lap_count; i++) {
        sumSwolf += _session.lap_data[i].swolf;
        sumSpm   += _session.lap_data[i].stroke_rate_spm;
        if (_session.lap_data[i].stroke_count > 0) {
            sumDps += _session.lap_data[i].dps_m_per_stroke;
            dpsCount++;
        }
    }
    float avgSwolf = (_session.lap_count > 0) ? (sumSwolf / _session.lap_count) : 0.0f;
    float avgSpm   = (_session.lap_count > 0) ? (sumSpm   / _session.lap_count) : 0.0f;
    float avgDps   = (dpsCount > 0)           ? (sumDps   / dpsCount)           : 0.0f;

    // Top-level scalars
    doc["id"]            = _session.id;
    doc["start_ms"]      = _session.start_ms;
    doc["end_ms"]        = _session.end_ms;
    doc["duration_s"]    = serialized(String(durS, 1));
    doc["pool_m"]        = _session.pool_length_m;
    doc["laps"]          = _session.lap_count;
    doc["total_strokes"] = totalStrokes;
    doc["total_dist_m"]  = (int)totalDistM;
    doc["avg_swolf"]     = serialized(String(avgSwolf, 1));
    doc["avg_spm"]       = serialized(String(avgSpm, 1));
    doc["avg_dps"]       = serialized(String(avgDps, 2));

    // Per-lap array — ArduinoJson v7 API
    JsonArray lapArr = doc["lap_data"].to<JsonArray>();
    for (uint16_t i = 0; i < _session.lap_count; i++) {
        const LapRecord& lr = _session.lap_data[i];
        JsonObject lObj = lapArr.add<JsonObject>();
        lObj["n"]       = lr.lap_number;
        lObj["t_s"]     = serialized(String(lr.duration_s, 1));
        lObj["strokes"] = lr.stroke_count;
        lObj["swolf"]   = serialized(String(lr.swolf, 1));
        lObj["spm"]     = serialized(String(lr.stroke_rate_spm, 1));
        lObj["dps"]     = serialized(String(lr.dps_m_per_stroke, 2));
    }

    // Rest intervals array — ArduinoJson v7 API
    JsonArray restArr = doc["rests"].to<JsonArray>();
    for (uint16_t i = 0; i < _session.rest_count; i++) {
        const RestRecord& rr = _session.rests[i];
        JsonObject rObj = restArr.add<JsonObject>();
        rObj["start_ms"] = rr.start_ms;
        rObj["dur_s"]    = serialized(String(rr.duration_s, 1));
    }
}