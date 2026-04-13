/**
 * @file    stroke_detector.cpp
 * @brief   Swim stroke detection implementation for SwimTrack.
 *
 *          Implements the StrokeDetector class declared in stroke_detector.h.
 *
 *          Key design decisions:
 *            - Baseline is updated by a very slow EMA (alpha=0.02) ONLY while
 *              the FSM is in the BELOW state.  This prevents arm-swing peaks
 *              from biasing the gravity estimate upward.
 *            - Stroke is counted on the ABOVE → BELOW falling edge (peak has
 *              passed), not on the rising edge, to count complete strokes.
 *            - STROKE_MIN_GAP_MS prevents double-counting within one arm cycle;
 *              the gate is checked on the BELOW → ABOVE rising edge.
 *            - Stroke rate uses the average of the last STROKE_RATE_WINDOW
 *              inter-stroke intervals stored in a circular buffer.
 *            - Classifier window: every call to update() pushes ax and gz into
 *              50-sample circular buffers.  _classifyStroke() uses min/max over
 *              this window for a depth-2 decision tree (99.2% CV accuracy).
 *
 * @author  SwimTrack Firmware Team
 * @date    2026-04
 */

#include "stroke_detector.h"

// ============================================================
//  FREE FUNCTION — strokeTypeName()
// ============================================================

/**
 * @brief Return a human-readable C-string for a StrokeType enum value.
 *
 * @param t  StrokeType value.
 * @return const char*  Name string; "UNKNOWN" for unrecognised values.
 */
const char* strokeTypeName(StrokeType t)
{
    switch (t) {
        case StrokeType::FREESTYLE:    return "FREESTYLE";
        case StrokeType::BACKSTROKE:   return "BACKSTROKE";
        case StrokeType::BREASTSTROKE: return "BREASTSTROKE";
        case StrokeType::BUTTERFLY:    return "BUTTERFLY";
        default:                       return "UNKNOWN";
    }
}

// ============================================================
//  CONSTRUCTOR
// ============================================================

/**
 * @brief Construct a StrokeDetector in a safe default state.
 *        Call begin() before first use to seed the baseline filter.
 */
StrokeDetector::StrokeDetector()
    : _state(State::BELOW),
      _baselineFilter(BASELINE_ALPHA),
      _strokeCount(0),
      _lastStrokeMs(0),
      _strokeType(StrokeType::UNKNOWN),
      _rateSpm(0.0f),
      _intervalHead(0),
      _intervalCount(0),
      _classHead(0),
      _classWindowFull(false)
{
    memset(_intervals, 0, sizeof(_intervals));
    memset(_axWindow,  0, sizeof(_axWindow));
    memset(_gxWindow,  0, sizeof(_gxWindow));
}

// ============================================================
//  begin()
// ============================================================

/**
 * @brief Fully initialise (or re-initialise) the stroke detector.
 *
 *        Seeds the baseline filter to 1.0 g (expected static value when the
 *        wrist is at rest) so the threshold is valid from the first sample.
 *        Resets all counters, interval history, and classifier window.
 */
void StrokeDetector::begin()
{
    _state        = State::BELOW;
    _strokeCount  = 0;
    _lastStrokeMs = 0;
    _strokeType   = StrokeType::UNKNOWN;
    _rateSpm      = 0.0f;
    _intervalHead  = 0;
    _intervalCount = 0;
    memset(_intervals, 0, sizeof(_intervals));

    // Reset classifier window
    _classHead       = 0;
    _classWindowFull = false;
    memset(_axWindow, 0, sizeof(_axWindow));
    memset(_gxWindow, 0, sizeof(_gxWindow));

    // Seed baseline to 1.0 g — gravity level when stationary
    _baselineFilter.reset(1.0f);

    DBG("STROKE", "StrokeDetector initialised. "
                  "threshold_g=baseline+%.4f, gap_ms=%d, rate_window=%d, classifier_window=%d",
        (float)STROKE_THRESHOLD_G, (int)STROKE_MIN_GAP_MS,
        (int)STROKE_RATE_WINDOW, (int)CLASSIFIER_WINDOW);
}

// ============================================================
//  update() — main per-sample function
// ============================================================

/**
 * @brief Process one filtered magnitude sample through the stroke FSM.
 *
 *        Classifier buffer update:
 *          ax and gz are pushed into their 50-sample circular buffers on
 *          EVERY call, regardless of FSM state.  This ensures the window
 *          always represents the most recent 1 second of motion.
 *
 *        Baseline update rule:
 *          Only advance the slow EMA while in BELOW state.  This keeps the
 *          baseline anchored to the quiet gravity level and prevents the
 *          dynamic threshold from drifting up during energetic swimming.
 *
 *        FSM transitions:
 *          BELOW → ABOVE: filtMag > (baseline + STROKE_THRESHOLD_G)
 *                         AND (nowMs - lastStrokeMs) >= STROKE_MIN_GAP_MS
 *          ABOVE → BELOW: filtMag <= threshold  → STROKE COUNTED
 *
 * @param filtMag  EMA-filtered acceleration magnitude [g].
 * @param ax       Accelerometer X axis [g] — fed to classifier window.
 * @param gz       Gyroscope Z axis [dps] — fed to classifier window.
 * @param nowMs    Current millis() timestamp.
 * @return true if a stroke was confirmed on this sample (falling edge).
 */
bool StrokeDetector::update(float filtMag, float ax, float gz, uint32_t nowMs)
{
    // ---- Fill classifier window buffers (every sample, regardless of state) ----
    _axWindow[_classHead] = ax;
    _gxWindow[_classHead] = gz;
    _classHead = (_classHead + 1) % CLASSIFIER_WINDOW;
    if (!_classWindowFull && _classHead == 0) {
        _classWindowFull = true;
    }

    // ---- Update baseline only when below threshold (quiet phase) ----
    if (_state == State::BELOW) {
        _baselineFilter.update(filtMag);
    }

    const float thresh = threshold();   // baseline + STROKE_THRESHOLD_G
    bool newStroke = false;

    switch (_state) {

        case State::BELOW:
            // Rising edge: enter ABOVE only if gap guard passes
            if (filtMag > thresh) {
                const uint32_t gap = (_lastStrokeMs == 0)
                                     ? STROKE_MIN_GAP_MS   // first stroke: always OK
                                     : (nowMs - _lastStrokeMs);
                if (gap >= STROKE_MIN_GAP_MS) {
                    _state = State::ABOVE;
                    DBG("STROKE", "Rising edge @ %.3f g (thresh=%.3f, gap=%lu ms)",
                        filtMag, thresh, (unsigned long)gap);
                } else {
                    DBG("STROKE", "Rising edge suppressed — gap %lu ms < %d ms",
                        (unsigned long)gap, (int)STROKE_MIN_GAP_MS);
                }
            }
            break;

        case State::ABOVE:
            // Falling edge: stroke confirmed
            if (filtMag <= thresh) {
                _state = State::BELOW;

                // Update interval buffer
                if (_lastStrokeMs > 0) {
                    _pushInterval(nowMs - _lastStrokeMs);
                    _updateRate();
                }

                _lastStrokeMs = nowMs;
                _strokeCount++;
                _strokeType   = _classifyStroke();
                newStroke     = true;

                DBG("STROKE", "Falling edge → stroke #%lu | rate=%.1f spm | type=%s",
                    (unsigned long)_strokeCount, _rateSpm,
                    strokeTypeName(_strokeType));
            }
            break;
    }

    return newStroke;
}

// Implementation continues in stroke_detector_part2.cpp