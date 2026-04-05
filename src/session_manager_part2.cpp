/**
 * @file    session_manager_part2.cpp
 * @brief   SessionManager query API and accessors.
 *
 *          Contains: listSessions(), printSession(), deleteSession(),
 *          printFSInfo(), currentLapCount(), currentRestCount().
 *
 *          Updated for ArduinoJson v7: StaticJsonDocument → JsonDocument.
 *
 * @author  SwimTrack Firmware Team
 * @date    2025-01-01
 */

#include "session_manager.h"

/**
 * @brief List all sessions stored in /sessions on LittleFS.
 *        Reads only top-level scalar fields from each file using a small
 *        JsonDocument, then prints a formatted table to Serial.
 * @return uint8_t  Number of session files found.
 */
void SessionManager::listSessions()
{
    DBG("SESSION", "Listing sessions in %s ...", SESSION_DIR);

    File dir = LittleFS.open(SESSION_DIR);
    if (!dir || !dir.isDirectory()) {
        DBG("SESSION", "ERROR: Cannot open %s", SESSION_DIR);
        Serial.println("[SESSION] No sessions directory found");
        return;
    }

    uint8_t count = 0;
    Serial.println("[SESSION] --- Stored Sessions ---");
    Serial.println("[SESSION]  ID          | Pool | Laps | Strokes | Dist  | Avg SWOLF");
    Serial.println("[SESSION] -------------------------------------------------------");

    File entry = dir.openNextFile();
    while (entry) {
        if (!entry.isDirectory()) {
            // ArduinoJson v7: use JsonDocument (replaces StaticJsonDocument<512>)
            JsonDocument summary;
            DeserializationError err = deserializeJson(summary, entry);
            if (!err) {
                uint32_t sid   = summary["id"]            | 0;
                uint8_t  pool  = summary["pool_m"]        | 0;
                uint16_t laps  = summary["laps"]          | 0;
                uint32_t strk  = summary["total_strokes"] | 0;
                float    dist  = summary["total_dist_m"]  | 0.0f;
                float    swolf = summary["avg_swolf"]     | 0.0f;
                Serial.printf("[SESSION]  %-12lu|  %2d m |  %3u |    %4lu |%5.0f m|   %.1f\n",
                              (unsigned long)sid, pool, laps,
                              (unsigned long)strk, dist, swolf);
                count++;
            } else {
                DBG("SESSION", "WARNING: Could not parse %s: %s",
                    entry.name(), err.c_str());
            }
        }
        entry.close();
        entry = dir.openNextFile();
    }
    dir.close();

    if (count == 0) Serial.println("[SESSION]  (no sessions found)");
    Serial.printf("[SESSION] --- %u session(s) total ---\n", count);
}

/**
 * @brief Stream the full JSON of one session file to Serial.
 * @param session_id  ID of the session to print.
 * @return true if found and printed, false otherwise.
 */
void SessionManager::printSession(uint32_t session_id)
{
    char path[48];
    _buildPath(session_id, path, sizeof(path));

    if (!LittleFS.exists(path)) {
        Serial.printf("[SESSION] Session %lu not found\n", (unsigned long)session_id);
        return;
    }

    File f = LittleFS.open(path, "r");
    if (!f) { DBG("SESSION", "Cannot open %s", path); return; }

    Serial.printf("[SESSION] File: %s (%u bytes)\n", path, (unsigned)f.size());
    Serial.println("[SESSION] Content:");
    uint8_t buf[256];
    while (f.available()) {
        size_t n = f.readBytes((char*)buf, sizeof(buf));
        Serial.write(buf, n);
    }
    Serial.println();
    f.close();
}

/**
 * @brief Delete one session file from LittleFS.
 * @param session_id  ID to delete.
 * @return true if deleted, false if not found.
 */
bool SessionManager::deleteSession(uint32_t session_id)
{
    char path[48];
    _buildPath(session_id, path, sizeof(path));

    if (!LittleFS.exists(path)) {
        Serial.printf("[SESSION] Delete failed — %lu not found\n",
                      (unsigned long)session_id);
        return false;
    }
    if (LittleFS.remove(path)) {
        Serial.printf("[SESSION] Deleted: %s\n", path);
        return true;
    }
    DBG("SESSION", "ERROR: Could not delete %s", path);
    return false;
}

/** @brief Print LittleFS total and used space to Serial. */
void SessionManager::printFSInfo()
{
    size_t total = LittleFS.totalBytes();
    size_t used  = LittleFS.usedBytes();
    DBG("SESSION", "LittleFS: %u KB used / %u KB total (%.1f%% full)",
        (unsigned)(used / 1024), (unsigned)(total / 1024),
        total > 0 ? (100.0f * (float)used / (float)total) : 0.0f);
    Serial.printf("[SESSION] Flash: %u KB used / %u KB total\n",
                  (unsigned)(used / 1024), (unsigned)(total / 1024));
}

uint16_t SessionManager::currentLapCount() const
{
    return _active ? _session.lap_count : 0;
}

uint16_t SessionManager::currentRestCount() const
{
    return _active ? _session.rest_count : 0;
}

// Private helpers in session_manager_part3.cpp