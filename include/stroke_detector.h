/**
 * @file    stroke_detector.h
 * @brief   Swim stroke detection interface for SwimTrack.
 *
 *          Detects individual strokes from filtered acceleration magnitude
 *          using a two-state FSM (BELOW / ABOVE threshold) with a dynamic
 *          baseline that slowly tracks the gravity level.
 *
 *          Algorithm overview:
 *            - A slow EMAFilter (BASELINE_ALPHA = 0.02) tracks the "quiet"
 *              accel magnitude, updated only when the FSM is in BELOW state
 *              to prevent arm-swing peaks from inflating the baseline.
 *            - Dynamic threshold = baseline + STROKE_THRESHOLD_G.
 *            - FSM transitions BELOW → ABOVE when filtMag > threshold AND
 *              time since last stroke >= STROKE_MIN_GAP_MS.
 *            - Stroke is counted on the ABOVE → BELOW falling edge.
 *            - Stroke rate (spm) is derived from a circular buffer of the
 *              last STROKE_RATE_WINDOW inter-stroke intervals.
 *
 * @author  SwimTrack Firmware Team
 * @date    2025-01-01
 */

#pragma once

#include <Arduino.h>
#include "config.h"
#include "imu_filters.h"

// ============================================================
//  MODULE-LOCAL CONSTANTS  (guarded so callers can override)
// ============================================================

/** Number of recent inter-stroke intervals used to compute stroke rate. */
#ifndef STROKE_RATE_WINDOW
  #define STROKE_RATE_WINDOW  5
#endif

/** Alpha for the slow EMA baseline tracker (gravity level estimation). */
#ifndef BASELINE_ALPHA
  #define BASELINE_ALPHA  0.02f
#endif

// ============================================================
//  STROKE TYPE ENUM
// ============================================================

/**
 * @brief Classification of swimming stroke style.
 *
 *        UNKNOWN is the default; FREESTYLE is the placeholder returned by
 *        the current _classifyStroke() stub.  Full classification will be
 *        added in a later prompt using gyro signature analysis.
 */
enum class StrokeType : uint8_t {
    UNKNOWN      = 0,
    FREESTYLE    = 1,
    BACKSTROKE   = 2,
    BREASTSTROKE = 3,
    BUTTERFLY    = 4
};

/**
 * @brief Return a human-readable name for a StrokeType value.
 *
 * @param t  StrokeType enum value.
 * @return const char*  e.g. "FREESTYLE", "UNKNOWN".
 */
const char* strokeTypeName(StrokeType t);

// ============================================================
//  STROKE DETECTOR CLASS
// ============================================================

/**
 * @brief Detects individual swim strokes from filtered accel magnitude.
 *
 * Usage:
 * @code
 *   StrokeDetector det;
 *   det.begin();
 *
 *   // In 50 Hz sample loop:
 *   if (det.update(filtMag, millis())) {
 *       Serial.printf("Stroke #%lu at %.1f spm\n",
 *                     det.strokeCount(), det.strokeRateSpm());
 *   }
 * @endcode
 */
class StrokeDetector {
public:

    // --------------------------------------------------------
    //  Lifecycle
    // --------------------------------------------------------

    /** @brief Default constructor.  Call begin() before use. */
    StrokeDetector();

    /**
     * @brief Initialise (or re-initialise) detector state.
     *
     *        Resets stroke count, inter-stroke interval buffer, FSM state,
     *        and seeds the baseline filter to 1.0 g.
     *        Call once in setup() and again at session start.
     */
    void begin();

    // --------------------------------------------------------
    //  Core update — call once per 50 Hz IMU sample
    // --------------------------------------------------------

    /**
     * @brief Feed one filtered magnitude sample into the stroke detector.
     *
     *        Updates the dynamic baseline (BELOW state only), runs the
     *        two-state FSM, and on a confirmed stroke: increments count,
     *        updates the inter-stroke interval buffer, and classifies style.
     *
     * @param filtMag  Filtered accel magnitude [g] from EMAFilter.
     * @param nowMs    Current millis() timestamp.
     * @return true if a new stroke was detected on this sample (falling edge
     *         of the ABOVE state).
     */
    bool update(float filtMag, uint32_t nowMs);

    // --------------------------------------------------------
    //  Accessors
    // --------------------------------------------------------

    /**
     * @brief Return total strokes counted since begin() or last reset().
     * @return uint32_t  Cumulative stroke count.
     */
    uint32_t strokeCount() const;

    /**
     * @brief Return the smoothed stroke rate in strokes per minute.
     *
     *        Computed from the average of the last STROKE_RATE_WINDOW
     *        inter-stroke intervals.  Returns 0.0 until at least two
     *        strokes have been detected.
     *
     * @return float  Stroke rate [spm].
     */
    float strokeRateSpm() const;

    /**
     * @brief Return the millis() timestamp of the most recent stroke.
     * @return uint32_t  Timestamp [ms], 0 if no stroke yet.
     */
    uint32_t lastStrokeTime() const;

    /**
     * @brief Return the classified stroke type of the most recent stroke.
     * @return StrokeType  Always FREESTYLE in the current stub.
     */
    StrokeType strokeType() const;

    /**
     * @brief Return the current dynamic baseline magnitude [g].
     *
     *        This is the slow-EMA estimate of the gravity-level at rest.
     *        Useful for Serial Plotter / diagnostic output.
     *
     * @return float  Baseline [g].
     */
    float baseline() const;

    /**
     * @brief Return the current detection threshold [g].
     *
     *        threshold = baseline() + STROKE_THRESHOLD_G.
     *
     * @return float  Threshold [g].
     */
    float threshold() const;

    /**
     * @brief Return true if the FSM is currently in the ABOVE state.
     *
     *        Useful for drawing a square-wave "above" trace in Serial Plotter.
     *
     * @return bool  true while filtMag > threshold.
     */
    bool isAboveThreshold() const;

    // --------------------------------------------------------
    //  Control
    // --------------------------------------------------------

    /**
     * @brief Reset stroke count and interval history; keep baseline filter.
     *
     *        Use for mid-session resets or Serial command 'r'.
     *        The baseline filter retains its current state so the threshold
     *        remains calibrated immediately after reset.
     */
    void reset();

private:

    // --------------------------------------------------------
    //  FSM states
    // --------------------------------------------------------

    /** Two-state detection FSM. */
    enum class State : uint8_t { BELOW, ABOVE };

    // --------------------------------------------------------
    //  Private helpers
    // --------------------------------------------------------

    /**
     * @brief Push a new inter-stroke interval into the circular buffer.
     *
     * @param intervalMs  Time between the two most recent strokes [ms].
     */
    void _pushInterval(uint32_t intervalMs);

    /**
     * @brief Recalculate _rateSpm from the interval buffer.
     *        Called after every new stroke.
     */
    void _updateRate();

    /**
     * @brief Classify the stroke type from recent IMU data.
     *
     *        Current implementation: always returns FREESTYLE.
     *        Will be expanded with gyro-signature analysis in Prompt 7.
     *
     * @return StrokeType  Classification result.
     */
    StrokeType _classifyStroke();

    // --------------------------------------------------------
    //  State
    // --------------------------------------------------------

    State      _state;           ///< Current FSM state
    EMAFilter  _baselineFilter;  ///< Slow EMA for gravity baseline (alpha=0.02)

    uint32_t   _strokeCount;     ///< Cumulative strokes since begin()
    uint32_t   _lastStrokeMs;    ///< millis() of last confirmed stroke
    StrokeType _strokeType;      ///< Type of most recent stroke

    float      _rateSpm;         ///< Cached strokes-per-minute

    /** Circular buffer of recent inter-stroke intervals [ms]. */
    uint32_t   _intervals[STROKE_RATE_WINDOW];
    uint8_t    _intervalHead;    ///< Next write index
    uint8_t    _intervalCount;   ///< Number of valid entries (0 … STROKE_RATE_WINDOW)
};
