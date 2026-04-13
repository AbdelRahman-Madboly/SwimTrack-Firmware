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
 *            - _classifyStroke() — real decision tree (replaces stub)
 *
 *          Classifier decision tree (depth 2, 99.2% CV accuracy, dry-land data):
 *            Features: ax_min and gx_max over a 50-sample (1 s) circular window.
 *            ax_min ≤ -0.35          → BACKSTROKE
 *            ax_min >  -0.35 AND gx_max ≤ 482.42  → FREESTYLE
 *            ax_min >  -0.35 AND gx_max >  482.42  → BACKSTROKE
 *
 * @author  SwimTrack Firmware Team
 * @date    2026-04
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
 *        Classifier window buffers are also cleared.
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

    // Reset classifier window
    _classHead       = 0;
    _classWindowFull = false;
    memset(_axWindow, 0, sizeof(_axWindow));
    memset(_gxWindow, 0, sizeof(_gxWindow));

    // NOTE: _baselineFilter is intentionally NOT reset here.
    DBG("STROKE", "reset() — count=0, history cleared, classifier window cleared, "
                  "baseline retained (%.3f g)", _baselineFilter.value());
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
 * @brief Classify swimming stroke style using a depth-2 decision tree.
 *
 *        Trained on 4494-row dry-land dataset (swimtrack_20260412_214046.csv).
 *        Cross-validation accuracy: 99.2%.
 *        Labels in training data: freestyle (72.3%), backstroke (27.7%).
 *
 *        Features extracted from the 50-sample (1 s) circular window:
 *          ax_min — minimum accelerometer X value over window [g]
 *          gx_max — maximum gyroscope Z value over window [dps]
 *                   (note: stored as gz in firmware, named gx in classifier
 *                    per the Python training script convention)
 *
 *        Decision tree rules:
 *          ax_min ≤ -0.35          → BACKSTROKE
 *          ax_min >  -0.35 AND gx_max ≤ 482.42  → FREESTYLE
 *          ax_min >  -0.35 AND gx_max >  482.42  → BACKSTROKE
 *
 *        Physical interpretation:
 *          Backstroke creates a stronger negative ax excursion as the arm
 *          pulls from overhead — this is the primary split.  The secondary
 *          gz split catches remaining backstroke cases where ax is less
 *          extreme (e.g., drill sets or partial strokes).
 *
 * @return StrokeType  BACKSTROKE, FREESTYLE, or UNKNOWN (window not full yet).
 */
StrokeType StrokeDetector::_classifyStroke()
{
    // Cannot classify until we have a full 50-sample window
    if (!_classWindowFull) {
        return StrokeType::UNKNOWN;
    }

    // Compute ax_min and gx_max over the entire circular window
    float ax_min = _axWindow[0];
    float gx_max = _gxWindow[0];

    for (uint8_t i = 1; i < CLASSIFIER_WINDOW; i++) {
        if (_axWindow[i] < ax_min) ax_min = _axWindow[i];
        if (_gxWindow[i] > gx_max) gx_max = _gxWindow[i];
    }

    DBG("STROKE", "Classifier: ax_min=%.3f gx_max=%.1f",
        ax_min, gx_max);

    // Decision tree — 99.2% CV accuracy on collected dry-land data
    if (ax_min <= -0.35f)  return StrokeType::BACKSTROKE;
    if (gx_max <= 482.42f) return StrokeType::FREESTYLE;
    return StrokeType::BACKSTROKE;
}