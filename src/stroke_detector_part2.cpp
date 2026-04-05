/**
 * @file    stroke_detector_part2.cpp
 * @brief   StrokeDetector — accessors, reset, and private helpers.
 *
 *          Continuation of stroke_detector.cpp.  Split to keep each file
 *          under 200 lines per SwimTrack code standards.
 *
 *          Contains:
 *            - All public accessors (strokeCount, strokeRateSpm, etc.)
 *            - reset()
 *            - _pushInterval() — circular interval buffer management
 *            - _updateRate()   — spm recalculation
 *            - _classifyStroke() stub
 *
 * @author  SwimTrack Firmware Team
 * @date    2025-01-01
 */

#include "stroke_detector.h"

// ============================================================
//  ACCESSORS
// ============================================================

/** @return Cumulative stroke count. */
uint32_t StrokeDetector::strokeCount() const  { return _strokeCount; }

/** @return Smoothed stroke rate [spm]; 0 until two strokes recorded. */
float StrokeDetector::strokeRateSpm() const   { return _rateSpm; }

/** @return millis() timestamp of most recent stroke (0 if none). */
uint32_t StrokeDetector::lastStrokeTime() const { return _lastStrokeMs; }

/** @return StrokeType of most recent stroke. */
StrokeType StrokeDetector::strokeType() const { return _strokeType; }

/** @return Current baseline estimate [g]. */
float StrokeDetector::baseline() const        { return _baselineFilter.value(); }

/**
 * @brief Dynamic detection threshold = baseline + STROKE_THRESHOLD_G.
 * @return float  Threshold [g].
 */
float StrokeDetector::threshold() const
{
    return _baselineFilter.value() + STROKE_THRESHOLD_G;
}

/** @return true while FSM is in ABOVE state (filtMag > threshold). */
bool StrokeDetector::isAboveThreshold() const
{
    return (_state == State::ABOVE);
}

// ============================================================
//  reset()
// ============================================================

/**
 * @brief Reset stroke count and interval history; preserve baseline filter.
 *
 *        The baseline retains its current state so the threshold stays
 *        calibrated immediately — no warm-up period required after reset.
 *        FSM returns to BELOW to prevent a phantom stroke on the next tick.
 */
void StrokeDetector::reset()
{
    _state         = State::BELOW;
    _strokeCount   = 0;
    _lastStrokeMs  = 0;
    _strokeType    = StrokeType::UNKNOWN;
    _rateSpm       = 0.0f;
    _intervalHead  = 0;
    _intervalCount = 0;
    memset(_intervals, 0, sizeof(_intervals));

    // NOTE: _baselineFilter is intentionally NOT reset here.
    DBG("STROKE", "reset() — count=0, history cleared, baseline retained (%.3f g)",
        _baselineFilter.value());
}

// ============================================================
//  PRIVATE — _pushInterval()
// ============================================================

/**
 * @brief Insert one inter-stroke interval into the circular buffer.
 *
 * @param intervalMs  Milliseconds between the two most recent strokes.
 */
void StrokeDetector::_pushInterval(uint32_t intervalMs)
{
    _intervals[_intervalHead] = intervalMs;
    _intervalHead = (_intervalHead + 1) % STROKE_RATE_WINDOW;
    if (_intervalCount < STROKE_RATE_WINDOW) {
        _intervalCount++;
    }
}

// ============================================================
//  PRIVATE — _updateRate()
// ============================================================

/**
 * @brief Recalculate _rateSpm from the current interval buffer contents.
 *
 *        Sums with integer arithmetic to avoid float accumulation error,
 *        then converts once at the end.  Returns 0 if no intervals yet.
 */
void StrokeDetector::_updateRate()
{
    if (_intervalCount == 0) {
        _rateSpm = 0.0f;
        return;
    }

    uint32_t sum = 0;
    for (uint8_t i = 0; i < _intervalCount; i++) {
        sum += _intervals[i];
    }

    const float avgMs = static_cast<float>(sum) / static_cast<float>(_intervalCount);
    _rateSpm = (avgMs > 0.0f) ? (60000.0f / avgMs) : 0.0f;
}

// ============================================================
//  PRIVATE — _classifyStroke()
// ============================================================

/**
 * @brief Classify the swimming stroke style from recent IMU data.
 *
 *        Stub: always returns FREESTYLE.  Full gyro-signature analysis
 *        (pitch/roll pattern matching per stroke cycle) will be added
 *        in a later prompt.
 *
 * @return StrokeType  Always StrokeType::FREESTYLE in this version.
 */
StrokeType StrokeDetector::_classifyStroke()
{
    return StrokeType::FREESTYLE;
}
