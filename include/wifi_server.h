/**
 * @file    wifi_server.h
 * @brief   WiFi SoftAP + HTTP REST API for SwimTrack.
 *
 *          Uses a free-function interface (wifiBegin / wifiLoop) rather than
 *          a class named WiFiServer to avoid a name collision with the Arduino
 *          WiFiServer class that is in scope from <WiFi.h>.
 *
 *          Shared object pointers are stored internally so every route handler
 *          can access live session, stroke, lap, and IMU data without global
 *          coupling in main.cpp.
 *
 *          REST endpoints:
 *            GET  /               → HTML dashboard placeholder
 *            GET  /api/status     → device mode, battery, uptime, client count
 *            GET  /api/live       → real-time IMU + stroke/lap snapshot
 *            GET  /api/sessions   → array of session summaries
 *            GET  /api/sessions/<id>    → full session JSON from LittleFS
 *            POST /api/session/start    → start recording {pool_length_m}
 *            POST /api/session/stop     → stop + save session
 *            POST /api/config           → update runtime config values
 *            DELETE /api/sessions/<id>  → delete a session file
 *
 *          All responses include:
 *            Content-Type: application/json
 *            Access-Control-Allow-Origin: *
 *
 * @author  SwimTrack Firmware Team
 * @date    2025-01-01
 */

#pragma once

#include <Arduino.h>
#include "config.h"
#include "session_manager.h"
#include "stroke_detector.h"
#include "lap_counter.h"
#include "mpu6500.h"         // IMUSample

// ============================================================
//  EXTERN GLOBALS  (defined in main.cpp)
// ============================================================

extern uint32_t lapStrokeCount;   ///< Per-lap stroke count maintained in main.cpp
extern uint8_t  poolLengthM;      ///< Current pool length setting (default 25 m)

// ============================================================
//  PUBLIC API — free functions
// ============================================================

/**
 * @brief Initialise the WiFi SoftAP and register all HTTP route handlers.
 *
 *        Stores the four pointers internally so route handlers can access live
 *        data.  Call once in Arduino setup() after all subsystems are ready.
 *
 *        Starts WiFi.softAP and WebServer on port 80.
 *        Prints [WIFI] AP started message with IP to Serial.
 *
 * @param sm   Pointer to the SessionManager instance.
 * @param sd   Pointer to the StrokeDetector instance.
 * @param lc   Pointer to the LapCounter instance.
 * @param s    Pointer to the IMUSample struct updated every 50 Hz tick.
 */
void wifiBegin(SessionManager* sm,
               StrokeDetector* sd,
               LapCounter*     lc,
               IMUSample*      s);

/**
 * @brief Service any pending HTTP client requests.
 *
 *        Must be called every iteration of Arduino loop().
 *        Delegates to WebServer::handleClient() internally.
 */
void wifiLoop();

/**
 * @brief Return true if at least one WiFi client is currently associated.
 *
 *        Wraps WiFi.softAPgetStationNum() > 0.
 *
 * @return bool  True when one or more phones/devices are connected.
 */
bool wifiIsClientConnected();
