/**
 * @file    session_manager.cpp
 * @brief   Session manager implementation for SwimTrack.
 *
 *          Implementation notes
 *          ─────────────────────
 *          LittleFS is used instead of SPIFFS because:
 *            - It is the recommended filesystem for ESP32 Arduino since 2021.
 *            - Better wear levelling and power-fail safety.
 *            - API is identical to SPIFFS for our use case.
 *
 *          JsonDocument is allocated on the heap only during
 *          stopSession() and listSessions(), then freed immediately.
 *          This avoids keeping 20 KB live in SRAM during recording.
 *
 *          JSON is written via serializeJson(doc, file) which streams
 *          directly to the file handle without a full intermediate string
 *          buffer — important on a device with 320 KB SRAM (ESP32-S2 has
 *          2 MB PSRAM available but we stay conservative).
 *
 * @author  SwimTrack Firmware Team
 * @date    2025-01-01
 */
#include "session_manager.h"
//  CONSTRUCTOR
SessionManager::SessionManager()
    : _active(false)
    , _lastSavedId(0)
{
    memset(&_session, 0, sizeof(_session));
}
//  begin()
/**
 * @brief Mount LittleFS and ensure the /sessions directory exists.
 *
 *        LittleFS.begin(formatOnFail=true) will auto-format a blank flash
 *        partition on the first boot.  This is safe for development; in
 *        production you may want to pass false and handle the error.
 *
 * @return true if mount succeeded, false otherwise.
 */
bool SessionManager::begin()
{
    DBG("SESSION", "Mounting LittleFS ...");
    if (!LittleFS.begin(true)) {   // true = format if mount fails
        DBG("SESSION", "ERROR: LittleFS mount failed");
        return false;
    }
    DBG("SESSION", "LittleFS mounted OK");
    printFSInfo();
    // Create /sessions directory if it does not exist
    if (!LittleFS.exists(SESSION_DIR)) {
        if (LittleFS.mkdir(SESSION_DIR)) {
            DBG("SESSION", "Created directory: %s", SESSION_DIR);
        } else {
            DBG("SESSION", "WARNING: Could not create %s — writes will fail", SESSION_DIR);
        }
    } else {
        DBG("SESSION", "Directory %s exists", SESSION_DIR);
    }
    return true;
}
//  startSession()
/**
 * @brief Begin a new recording session.
 *
 *        If a session is already active it is stopped and saved first so
 *        data is never silently discarded.
 */
void SessionManager::startSession(uint8_t pool_length_m)
{
    if (_active) {
        DBG("SESSION", "WARNING: session already active — auto-stopping before new start");
        stopSession(_session.total_strokes);
    }
    memset(&_session, 0, sizeof(_session));
    _session.id           = millis();   // monotonic unique ID; replace with RTC later
    _session.start_ms     = millis();
    _session.pool_length_m= pool_length_m;
    _session.lap_count    = 0;
    _session.rest_count   = 0;
    _session.total_strokes= 0;

    _active = true;

    DBG("SESSION", "Started | id=%lu | pool=%d m", (unsigned long)_session.id, pool_length_m);
    Serial.printf("[SESSION] Started | ID: %lu | Pool: %d m\n",
                  (unsigned long)_session.id, pool_length_m);
}

//  stopSession()

/**
 * @brief Stop recording, serialise session to JSON, write to LittleFS.
 *
 *        The total_strokes parameter includes strokes from any incomplete
 *        final lap that was never confirmed as a LapRecord.
 *
 * @param totalStrokes  Cumulative stroke count from StrokeDetector.
 */
void SessionManager::stopSession(uint32_t totalStrokes)
{
    if (!_active) {
        DBG("SESSION", "stopSession() called but no session is active — ignored");
        return;
    }

    _session.end_ms        = millis();
    _session.total_strokes = totalStrokes;
    _active                = false;

    float durS = (float)(_session.end_ms - _session.start_ms) / 1000.0f;
    DBG("SESSION", "Stopping. ID=%lu | dur=%.1fs | laps=%u | strokes=%lu",
        (unsigned long)_session.id, durS,
        _session.lap_count, (unsigned long)totalStrokes);

    // ── Build JSON ─────────────────────────────────────────────
    JsonDocument doc;
    _buildJson(totalStrokes, doc);

    // ── Build file path ────────────────────────────────────────
    char path[48];
    _buildPath(_session.id, path, sizeof(path));

    // ── Write to LittleFS ──────────────────────────────────────
    File f = LittleFS.open(path, "w");
    if (!f) {
        DBG("SESSION", "ERROR: Could not open %s for writing", path);
        return;
    }

    size_t written = serializeJson(doc, f);
    f.close();

    _lastSavedId = _session.id;

    DBG("SESSION", "Saved: %s (%u bytes)", path, (unsigned)written);
    Serial.printf("[SESSION] Saved: %s (%u bytes)\n", path, (unsigned)written);

    // ── Print a compact summary to Serial Monitor ──────────────
    Serial.printf("[SESSION] Summary | Laps: %u | Strokes: %lu | Dist: %u m | Duration: %.1f s\n",
                  _session.lap_count,
                  (unsigned long)totalStrokes,
                  (unsigned)(_session.lap_count * _session.pool_length_m),
                  durS);

    // Optional: also stream the full JSON to Serial for verification
    Serial.print("[SESSION] JSON: ");
    serializeJsonPretty(doc, Serial);
    Serial.println();
}

//  recordLap()

/**
 * @brief Append a LapRecord to the session's lap_data array.
 *
 *        If SESSION_MAX_LAPS is reached, the record is dropped and a
 *        warning is printed.  The session can still be stopped normally.
 */
void SessionManager::recordLap(const LapRecord& lap)
{
    if (!_active) return;

    if (_session.lap_count >= SESSION_MAX_LAPS) {
        DBG("SESSION", "WARNING: lap_data full (%d laps max) — lap #%u dropped",
            SESSION_MAX_LAPS, lap.lap_number);
        return;
    }

    _session.lap_data[_session.lap_count] = lap;
    _session.lap_count++;

    DBG("SESSION", "Recorded lap #%u (%.1fs, %lu strokes, SWOLF=%.1f)",
        lap.lap_number, lap.duration_s,
        (unsigned long)lap.stroke_count, lap.swolf);
}

//  recordRest()

/**
 * @brief Append a RestRecord to the session's rests array.
 */
void SessionManager::recordRest(uint32_t start_ms, float dur_s)
{
    if (!_active) return;

    if (_session.rest_count >= SESSION_MAX_RESTS) {
        DBG("SESSION", "WARNING: rests[] full (%d max) — rest dropped", SESSION_MAX_RESTS);
        return;
    }

    _session.rests[_session.rest_count].start_ms = start_ms;
    _session.rests[_session.rest_count].duration_s = dur_s;
    _session.rest_count++;

    DBG("SESSION", "Recorded rest: start=%lu ms, dur=%.1f s", (unsigned long)start_ms, dur_s);
}

// Implementation continues in session_manager_part2.cpp