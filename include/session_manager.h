/**
 * @file    session_manager.h
 * @brief   Session lifecycle, storage, and JSON serialisation for SwimTrack.
 *
 *          Manages one swim session at a time.  Accumulates LapRecords and
 *          RestRecords in SRAM, then serialises to LittleFS as JSON on stop.
 *
 *          New in v2.0.0:
 *            avg_dps — session-level average Distance Per Stroke [m/stroke].
 *                      Computed as mean of per-lap DPS values in _buildJson().
 *                      Added to all JSON outputs and /api/sessions responses.
 *
 * @author  SwimTrack Firmware Team
 * @date    2026-03-30
 * @version 2.0.0  (final hardware + DPS)
 */

#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "config.h"
#include "lap_counter.h"

// ============================================================
//  SESSION DATA STRUCTURES (SRAM)
// ============================================================

/** One rest interval recorded during a session. */
struct RestRecord {
    uint32_t start_ms;    ///< millis() when rest began
    float    duration_s;  ///< how long the rest lasted [s]
};

/** Complete in-memory session. */
struct SessionData {
    uint32_t   id;                             ///< Unique ID (millis at start)
    uint32_t   start_ms;
    uint32_t   end_ms;
    uint8_t    pool_length_m;
    uint16_t   lap_count;
    uint16_t   rest_count;
    uint32_t   total_strokes;
    LapRecord  lap_data[SESSION_MAX_LAPS];
    RestRecord rests[SESSION_MAX_RESTS];
};

// ============================================================
//  SESSION MANAGER CLASS
// ============================================================

class SessionManager {
public:

    SessionManager();

    /**
     * @brief Mount LittleFS and create /sessions directory.
     * @return true if mount succeeded.
     */
    bool begin();

    // ---- Session lifecycle ----

    /**
     * @brief Start a new recording session.
     * @param pool_length_m  Pool length in metres (25 or 50 typical).
     */
    void startSession(uint8_t pool_length_m = DEFAULT_POOL_LENGTH_M);

    /**
     * @brief Stop the active session, build JSON (including avg_dps),
     *        and save to LittleFS.
     * @param totalStrokes  Total stroke count from StrokeDetector at stop time.
     */
    void stopSession(uint32_t totalStrokes);

    /** @return True if a session is currently being recorded. */
    bool isActive() const { return _active; }

    // ---- Real-time accumulation ----

    /** @brief Append a completed LapRecord (including its dps_m_per_stroke). */
    void recordLap(const LapRecord& lap);

    /** @brief Append a completed rest interval. */
    void recordRest(uint32_t startMs, float durationS);

    // ---- Stored session access ----

    void listSessions();
    void printSession(uint32_t session_id);
    bool deleteSession(uint32_t session_id);
    void printFSInfo();

    // ---- Accessors for WiFi API ----

    uint16_t currentLapCount()  const;
    uint16_t currentRestCount() const;

    /** @return ID of the last successfully saved session (0 if none). */
    uint32_t lastSavedId() const { return _lastSavedId; }

    /** @return Pool length of the current (or last) session. */
    uint8_t  poolLengthM()  const { return _session.pool_length_m; }

private:

    bool        _active;
    SessionData _session;
    uint32_t    _lastSavedId;

    void _buildPath(uint32_t session_id, char* buf, size_t bufLen) const;

    /**
     * @brief Compute all derived metrics — including avg_dps — and serialise.
     *
     *        avg_dps = mean of dps_m_per_stroke across all laps that have
     *                  at least one stroke (laps with stroke_count == 0 excluded).
     */
    void _buildJson(uint32_t totalStrokes, JsonDocument& doc) const;
};