/**
 * @file    imu_filters.h
 * @brief   IMU signal filtering for SwimTrack.
 *
 *          Provides an Exponential Moving Average (EMA) low-pass filter class
 *          and helper functions for computing and filtering acceleration magnitude.
 *
 *          EMA formula:  y[n] = alpha * x[n] + (1 - alpha) * y[n-1]
 *          where alpha in (0, 1]:
 *            - alpha = 1.0  → no filtering (pass-through)
 *            - alpha → 0.0  → heavy smoothing, high lag
 *            - alpha = 0.3  → recommended for stroke detection (EMA_ALPHA in config.h)
 *
 *          The filter is "seeded" on the first update() call so that the output
 *          starts at a physically meaningful value rather than ramping up from 0.
 *
 * @author  SwimTrack Firmware Team
 * @date    2025-01-01
 */

#pragma once

#include <Arduino.h>
#include <math.h>
#include "config.h"

// Forward-declare IMUSample so the overloaded accelMagnitude() can reference it
// without pulling in the full mpu6500.h here.
struct IMUSample;

// ============================================================
//  EMA LOW-PASS FILTER CLASS
// ============================================================

/**
 * @brief Exponential Moving Average (EMA) single-pole IIR low-pass filter.
 *
 *        Lightweight stateful filter suitable for real-time embedded use.
 *        Maintains one float of state (_prev), so each signal that needs
 *        independent filtering should have its own EMAFilter instance.
 *
 * Usage:
 * @code
 *   EMAFilter f(0.3f);           // alpha = 0.3
 *   f.reset(1.0f);               // seed to 1 g before first sample
 *   float smooth = f.update(raw);
 * @endcode
 */
class EMAFilter {
public:

    // --------------------------------------------------------
    //  Lifecycle
    // --------------------------------------------------------

    /**
     * @brief Construct a new EMAFilter with a given smoothing coefficient.
     *
     * @param alpha  Smoothing factor in range (0, 1].
     *               Lower values produce more smoothing with more phase lag.
     *               Pass EMA_ALPHA (0.3f) from config.h for stroke detection.
     */
    explicit EMAFilter(float alpha = EMA_ALPHA);

    // --------------------------------------------------------
    //  Core operation
    // --------------------------------------------------------

    /**
     * @brief Feed a new raw sample into the filter and get the smoothed output.
     *
     *        On the very first call the filter is "seeded" with the input value
     *        so there is no initial transient ramp from zero.
     *        Subsequent calls apply: y[n] = alpha * x[n] + (1 - alpha) * y[n-1]
     *
     * @param input  New raw measurement (e.g. accel magnitude in g).
     * @return float Filtered (smoothed) value.
     */
    float update(float input);

    /**
     * @brief Reset the filter state and optionally pre-load a seed value.
     *
     *        Call this when starting a new session or after a sensor re-init
     *        so stale state from the previous session does not bleed in.
     *        If seedValue != 0, the filter is marked as already seeded so the
     *        first update() applies the EMA recurrence rather than overwriting.
     *
     * @param seedValue  Initial value to pre-load into filter state (default 0).
     *                   Pass ~1.0f for accel-magnitude filters to avoid an
     *                   initial dip toward zero before the first sample.
     */
    void reset(float seedValue = 0.0f);

    // --------------------------------------------------------
    //  Accessors
    // --------------------------------------------------------

    /**
     * @brief Return the current smoothed output without pushing a new sample.
     * @return float Last filtered value.
     */
    float value() const { return _prev; }

    /**
     * @brief Change the alpha coefficient at runtime (e.g. switching modes).
     *        Clamps the value to the range (0.01, 1.0].
     *
     * @param alpha  New smoothing factor.
     */
    void setAlpha(float alpha);

    /**
     * @brief Return the currently configured alpha value.
     * @return float Alpha in (0.01, 1.0].
     */
    float alpha() const { return _alpha; }

private:

    float _alpha;    ///< Smoothing factor, clamped to (0.01, 1.0]
    float _prev;     ///< Previous filtered output (filter state)
    bool  _seeded;   ///< True after the first sample or an explicit seed
};

// ============================================================
//  FREE FUNCTIONS — MAGNITUDE & FILTERING
// ============================================================

/**
 * @brief Compute the Euclidean magnitude of the acceleration vector.
 *
 *        magnitude = sqrtf(ax² + ay² + az²)
 *
 *        Uses sqrtf() (single-precision) to avoid implicit double promotion
 *        on Xtensa-LX6/LX7.  When the sensor is stationary, the result
 *        equals 1.0 g (gravity).  During a swim stroke it rises above 1.0 g.
 *
 * @param ax  Acceleration along X axis [g].
 * @param ay  Acceleration along Y axis [g].
 * @param az  Acceleration along Z axis [g].
 * @return float  Magnitude in g (always >= 0).
 */
float accelMagnitude(float ax, float ay, float az);

/**
 * @brief Convenience overload: compute accel magnitude directly from an IMUSample.
 *
 *        Identical to accelMagnitude(s.ax, s.ay, s.az).
 *
 * @param s  Fully converted IMUSample (only ax, ay, az are used).
 * @return float  Magnitude in g.
 */
float accelMagnitude(const IMUSample& s);

/**
 * @brief Compute raw accel magnitude and immediately push it through an EMAFilter.
 *
 *        Equivalent to:
 *          float raw = accelMagnitude(ax, ay, az);
 *          return filter.update(raw);
 *
 *        Provided as a convenience so callers do not need to store the
 *        intermediate raw magnitude themselves.
 *
 * @param filter  EMAFilter instance to update (state is mutated in place).
 * @param ax      Acceleration X [g].
 * @param ay      Acceleration Y [g].
 * @param az      Acceleration Z [g].
 * @return float  Filtered acceleration magnitude in g.
 */
float filteredMagnitude(EMAFilter& filter, float ax, float ay, float az);
