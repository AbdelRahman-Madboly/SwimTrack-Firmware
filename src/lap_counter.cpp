/**
 * @file    lap_counter.cpp
 * @brief   Lap counting and rest detection — constructor, begin(), update().
 *          Turn FSM: IDLE→SPIKE→GLIDE_WAIT→confirmed. See lap_counter.h for detail.
 * @author  SwimTrack Firmware Team
 * @date    2025-01-01
 */
#include "lap_counter.h"
LapCounter::LapCounter()
    : _turnState(TurnState::IDLE)
    , _turnPending(false)
    , _spikeStartMs(0), _spikeEndMs(0), _glideStartMs(0)
    , _peakGyroZ(0.0f)
    , _lapCount(0)
    , _lapStartMs(0)
    , _currentLapElapsedS(0.0f)
    , _lastLap{}
    , _varHead(0), _varWindowFull(false)
    , _variance(0.0f)
    , _resting(false)
    , _restJustStarted(false), _restJustEnded(false)
    , _lowVarStartMs(0), _restStartMs(0)
    , _lastRestDurationS(0.0f)
{
    memset(_varWindow, 0, sizeof(_varWindow));
}
/**
 * @brief Full initialisation. Resets all state and starts a fresh session.
 */
void LapCounter::begin()
{
    _turnState       = TurnState::IDLE;
    _turnPending     = false;
    _spikeStartMs    = 0;
    _spikeEndMs      = 0;
    _glideStartMs    = 0;
    _peakGyroZ       = 0.0f;
    _lapCount            = 0;
    _lapStartMs          = 0;
    _currentLapElapsedS  = 0.0f;
    memset(&_lastLap, 0, sizeof(_lastLap));

    memset(_varWindow, 0, sizeof(_varWindow));
    _varHead        = 0;
    _varWindowFull  = false;
    _variance       = 0.0f;

    _resting            = false;
    _restJustStarted    = false;
    _restJustEnded      = false;
    _lowVarStartMs      = 0;
    _restStartMs        = 0;
    _lastRestDurationS  = 0.0f;

    DBG("LAP", "LapCounter initialised. GyroZ thresh=%.0f dps, min-lap=%d ms, "
               "glide-window=%d ms, rest-thresh=%.3f g², rest-dur=%d ms",
        (float)TURN_GYRO_Z_THRESH_DPS, (int)LAP_MIN_DURATION_MS,
        (int)TURN_GLIDE_WINDOW_MS, (float)REST_VARIANCE_THRESH,
        (int)REST_DURATION_MS);
}

void LapCounter::reset() { begin(); }

//  update() — master per-sample function

/**
 * @brief Run the turn FSM and rest detector on one new IMU sample.
 *
 *        Order of operations each call:
 *          1. Update the variance window (_updateVariance).
 *          2. Run the rest detector (_updateRest).
 *          3. Update current lap elapsed time.
 *          4. Run the turn detection FSM.
 *          5. Return true if a lap was completed.
 *
 * @param gyroZ_dps   Raw gyro Z [dps].
 * @param filtMag     Filtered accel magnitude [g].
 * @param lapStrokes  Strokes accumulated this lap by the caller.
 * @param rateSpm     Current stroke rate from StrokeDetector [spm].
 * @param nowMs       millis() at time of this sample.
 * @return true if a lap was registered.
 */
bool LapCounter::update(float gyroZ_dps, float filtMag,
                        uint32_t lapStrokes, float rateSpm,
                        uint32_t nowMs,
                        uint8_t poolLengthM)
{
    // Clear one-shot pulse flags from previous call
    _restJustStarted = false;
    _restJustEnded   = false;

    // 1. Variance window + rest
    _updateVariance(filtMag);
    _updateRest(nowMs);

    // 2. Current lap elapsed time
    if (_lapStartMs > 0) {
        _currentLapElapsedS = (float)(nowMs - _lapStartMs) / 1000.0f;
    }

    // 3. Turn FSM
    float absGyroZ = fabsf(gyroZ_dps);
    bool  lapCompleted = false;

    switch (_turnState) {

    // ── IDLE: watching for gyro spike ─────────────────────────
    case TurnState::IDLE: {
        if (absGyroZ > TURN_GYRO_Z_THRESH_DPS) {
            // Check minimum lap time guard
            bool lapTimeOk = (_lapStartMs == 0) ||
                             ((nowMs - _lapStartMs) >= (uint32_t)LAP_MIN_DURATION_MS);

            if (lapTimeOk) {
                _turnState    = TurnState::SPIKE;
                _turnPending  = true;
                _spikeStartMs = nowMs;
                _peakGyroZ    = absGyroZ;
                DBG("LAP", "Gyro spike detected: %.1f dps at t=%lu ms (lap guard OK)",
                    absGyroZ, (unsigned long)nowMs);
            } else {
                DBG("LAP", "Gyro spike %.1f dps ignored — lap guard (%lu ms < %d ms min)",
                    absGyroZ,
                    (unsigned long)(nowMs - _lapStartMs),
                    (int)LAP_MIN_DURATION_MS);
            }
        }
        break;
    }

    // ── SPIKE: spike in progress, waiting for it to drop ──────
    case TurnState::SPIKE: {
        if (absGyroZ > _peakGyroZ) {
            _peakGyroZ = absGyroZ;  // track maximum
        }

        if (absGyroZ <= TURN_GYRO_Z_THRESH_DPS) {
            // Spike just ended
            uint32_t spikeDur = nowMs - _spikeStartMs;

            if (spikeDur >= (uint32_t)TURN_SPIKE_MIN_MS) {
                // Valid spike — advance to glide confirmation
                _turnState   = TurnState::GLIDE_WAIT;
                _spikeEndMs  = nowMs;
                _glideStartMs = 0;  // not in glide yet
                DBG("LAP", "Spike ended: peak=%.1f dps, dur=%lu ms → waiting for glide",
                    _peakGyroZ, (unsigned long)spikeDur);
            } else {
                // Too brief — treat as noise, discard
                DBG("LAP", "Spike too short (%lu ms < %d ms min) — discarded",
                    (unsigned long)spikeDur, (int)TURN_SPIKE_MIN_MS);
                _turnState   = TurnState::IDLE;
                _turnPending = false;
            }
        }
        break;
    }

    // ── GLIDE_WAIT: confirming glide phase after spike ─────────
    case TurnState::GLIDE_WAIT: {
        uint32_t elapsed = nowMs - _spikeEndMs;

        if (elapsed >= (uint32_t)TURN_GLIDE_WINDOW_MS) {
            // Window expired without glide confirmation → false turn
            DBG("LAP", "Glide window expired (%d ms) — no glide seen, discarding turn",
                (int)TURN_GLIDE_WINDOW_MS);
            _turnState   = TurnState::IDLE;
            _turnPending = false;
            break;
        }

        if (filtMag < GLIDE_ACCEL_THRESH_G) {
            // In potential glide
            if (_glideStartMs == 0) {
                _glideStartMs = nowMs;  // start timing the glide
                DBG("LAP", "Glide candidate: filtMag=%.3f g", filtMag);
            }

            uint32_t glideDur = nowMs - _glideStartMs;
            if (glideDur >= (uint32_t)TURN_GLIDE_MIN_MS) {
                // Sustained glide confirmed → register the lap
                _confirmLap(lapStrokes, rateSpm, nowMs, poolLengthM);
                lapCompleted = true;
            }
        } else {
            // Accel too high — reset the glide timer (not gliding yet)
            if (_glideStartMs != 0) {
                DBG("LAP", "Glide interrupted (filtMag=%.3f g > %.3f g threshold) — reset timer",
                    filtMag, (float)GLIDE_ACCEL_THRESH_G);
            }
            _glideStartMs = 0;
        }
        break;
    }

    } // end switch

    return lapCompleted;
}

// Implementation continues in lap_counter_part2.cpp