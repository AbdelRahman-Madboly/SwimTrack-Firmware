/**
 * @file    lap_counter_part2.cpp
 * @brief   LapCounter private helpers: _confirmLap, _updateVariance, _updateRest.
 *
 *          Continuation of lap_counter.cpp.  Split to keep each file under
 *          200 lines per SwimTrack code standards.
 *
 * @author  SwimTrack Firmware Team
 * @date    2025-01-01
 */

#include "lap_counter.h"
// ============================================================
//  _confirmLap()
// ============================================================

/**
 * @brief Record a confirmed lap, emit the debug line, and reset the turn FSM.
 *
 *        SWOLF = strokes + lap_duration_seconds  (integer strokes + float seconds).
 *        This is the standard swimming efficiency metric; lower is better.
 *
 * @param lapStrokes  Caller's per-lap stroke counter.
 * @param rateSpm     Caller's current stroke rate.
 * @param nowMs       Confirmation timestamp.
 */
void LapCounter::_confirmLap(uint32_t lapStrokes, float rateSpm, uint32_t nowMs,
                             uint8_t poolLengthM)
{
    _lapCount++;

    uint32_t startMs  = (_lapStartMs == 0) ? 0 : _lapStartMs;
    float    durS     = (float)(nowMs - startMs) / 1000.0f;
    float    swolf    = (float)lapStrokes + durS;

    _lastLap.lap_number      = _lapCount;
    _lastLap.start_time_ms   = startMs;
    _lastLap.end_time_ms     = nowMs;
    _lastLap.duration_s      = durS;
    _lastLap.stroke_count    = lapStrokes;
    _lastLap.swolf           = swolf;
    _lastLap.stroke_rate_spm = rateSpm;
    _lastLap.dps_m_per_stroke = (lapStrokes > 0)
        ? (static_cast<float>(poolLengthM) / static_cast<float>(lapStrokes))
        : 0.0f;

    // Next lap starts from this confirmed turn
    _lapStartMs         = nowMs;
    _currentLapElapsedS = 0.0f;

    // Reset turn FSM
    _turnState   = TurnState::IDLE;
    _turnPending = false;

    DBG("LAP", "=== LAP #%u CONFIRMED ===", _lapCount);
    DBG("LAP", "  Time    : %.1f s", durS);
    DBG("LAP", "  Strokes : %lu", (unsigned long)lapStrokes);
    DBG("LAP", "  SWOLF   : %.1f  (strokes=%lu + time=%.1f s)",
        swolf, (unsigned long)lapStrokes, durS);
    DBG("LAP", "  Rate    : %.1f spm | Peak gyroZ: %.1f dps",
        rateSpm, _peakGyroZ);
    DBG("LAP", "  DPS     : %.2f m/stroke (pool=%d m)",
        _lastLap.dps_m_per_stroke, (int)poolLengthM);
}

// ============================================================
//  _updateVariance()  — Welford sliding-window variance
// ============================================================

/**
 * @brief Maintain a rolling 1-second variance of filtered accel magnitude.
 *
 *        Implementation uses a circular buffer of VAR_WINDOW_SIZE samples.
 *        Variance is recomputed from scratch each call from the window
 *        contents.  At VAR_WINDOW_SIZE=50 (1 s at 50 Hz) this is 50
 *        floating-point operations per sample — acceptable on ESP32-S2.
 *
 *        Note: a full Welford online update with old-sample removal is
 *        numerically sensitive; recomputing from the window each time is
 *        more robust for this window size and avoids accumulating error.
 *
 * @param mag  New filtered magnitude sample [g].
 */
void LapCounter::_updateVariance(float mag)
{
    // Store new sample, advance circular head
    _varWindow[_varHead] = mag;
    _varHead = (_varHead + 1) % VAR_WINDOW_SIZE;
    if (!_varWindowFull && _varHead == 0) {
        _varWindowFull = true;
    }

    uint8_t n = _varWindowFull ? VAR_WINDOW_SIZE : _varHead;
    if (n < 2) { _variance = 0.0f; return; }

    // Compute mean
    float sum = 0.0f;
    for (uint8_t i = 0; i < n; i++) sum += _varWindow[i];
    float mean = sum / (float)n;

    // Compute variance  σ² = Σ(x - mean)² / n
    float sq = 0.0f;
    for (uint8_t i = 0; i < n; i++) {
        float d = _varWindow[i] - mean;
        sq += d * d;
    }
    _variance = sq / (float)n;
}

// ============================================================
//  _updateRest()
// ============================================================

/**
 * @brief Determine rest state from the current variance.
 *
 *        Rest entry: variance < REST_VARIANCE_THRESH continuously for
 *                    REST_DURATION_MS milliseconds.
 *        Rest exit : variance >= REST_VARIANCE_THRESH at any point.
 *
 *        One-shot flags _restJustStarted / _restJustEnded are set on the
 *        transition sample so the caller can react to edge events without
 *        polling _resting every cycle.
 *
 * @param nowMs  Current millis() timestamp.
 */
void LapCounter::_updateRest(uint32_t nowMs)
{
    bool lowVar = (_variance < REST_VARIANCE_THRESH);

    if (!_resting) {
        // Not currently resting — look for entry condition
        if (lowVar) {
            if (_lowVarStartMs == 0) {
                _lowVarStartMs = nowMs;  // start timing
            } else if ((nowMs - _lowVarStartMs) >= (uint32_t)REST_DURATION_MS) {
                // Variance stayed low long enough → declare rest
                _resting         = true;
                _restJustStarted = true;
                _restStartMs     = _lowVarStartMs;
                DBG("LAP", "[REST] Started (low-var for %d ms, variance=%.4f g²)",
                    (int)REST_DURATION_MS, _variance);
            }
        } else {
            _lowVarStartMs = 0;  // reset timer if variance jumps back up
        }
    } else {
        // Currently resting — look for exit condition
        if (!lowVar) {
            float dur = (float)(nowMs - _restStartMs) / 1000.0f;
            _lastRestDurationS = dur;
            _resting           = false;
            _restJustEnded     = true;
            _lowVarStartMs     = 0;
            DBG("LAP", "[REST] Ended after %.1f s (variance=%.4f g²)", dur, _variance);
        }
    }
}