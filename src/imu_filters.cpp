/**
 * @file    imu_filters.cpp
 * @brief   IMU signal filtering implementation for SwimTrack.
 *
 *          Implements the EMAFilter class and accel-magnitude helpers declared
 *          in imu_filters.h.
 *
 *          Design notes:
 *            - sqrtf() (single-precision) is used instead of sqrt() to stay in
 *              float arithmetic and avoid implicit double promotion on Xtensa.
 *            - The filter is seeded on the first sample to prevent the output
 *              from ramping up from 0.0 g at startup.
 *            - alpha is clamped to (0.01, 1.0] inside setAlpha() to prevent
 *              an unstable filter state or division concerns in derived code.
 *
 * @author  SwimTrack Firmware Team
 * @date    2025-01-01
 */

#include "imu_filters.h"
#include "mpu6500.h"    // for full IMUSample definition

// ============================================================
//  EMAFilter — CONSTRUCTOR
// ============================================================

/**
 * @brief Initialise the filter with the given alpha; mark it as unseeded.
 *
 *        The filter output will be set to the first input value it receives,
 *        avoiding the startup transient that would occur if _prev started at 0.
 *
 * @param alpha  Smoothing coefficient, clamped internally to (0.01, 1.0].
 */
EMAFilter::EMAFilter(float alpha)
    : _prev(0.0f), _seeded(false)
{
    setAlpha(alpha);
}

// ============================================================
//  EMAFilter::update()
// ============================================================

/**
 * @brief Push a new sample through the EMA filter.
 *
 *        On the first call (_seeded == false) the filter state is initialised
 *        to the input value so the output starts at a physically meaningful
 *        value (e.g. ~1.0 g when the sensor is lying flat).
 *
 *        Subsequent calls apply: y[n] = alpha * x[n] + (1 - alpha) * y[n-1]
 *
 * @param input  Raw measurement (e.g. accel magnitude in g).
 * @return float Smoothed output.
 */
float EMAFilter::update(float input)
{
    if (!_seeded) {
        _prev   = input;
        _seeded = true;
        return _prev;
    }

    _prev = _alpha * input + (1.0f - _alpha) * _prev;
    return _prev;
}

// ============================================================
//  EMAFilter::reset()
// ============================================================

/**
 * @brief Clear filter state and optionally pre-load a seed value.
 *
 *        If seedValue != 0 the filter is marked as already seeded so that the
 *        next update() call applies the recurrence relation rather than
 *        overwriting _prev with the raw input.
 *
 *        Call at session start or after sensor re-init to avoid stale state.
 *
 * @param seedValue  Value to initialise the filter output to (default 0).
 *                   Pass ~1.0f for accel-magnitude filters to avoid an
 *                   initial dip before the first real sample arrives.
 */
void EMAFilter::reset(float seedValue)
{
    _prev   = seedValue;
    _seeded = (seedValue != 0.0f);  // explicit non-zero seed → treat as seeded
    DBG("FILT", "EMAFilter reset (seed=%.3f, alpha=%.2f, seeded=%d)",
        seedValue, _alpha, (int)_seeded);
}

// ============================================================
//  EMAFilter::setAlpha()
// ============================================================

/**
 * @brief Update the alpha coefficient with range clamping.
 *
 *        Values <= 0 are raised to 0.01 (extremely heavy smoothing but stable).
 *        Values > 1 are reduced to 1.0 (pass-through, no smoothing).
 *
 * @param alpha  Desired smoothing factor.
 */
void EMAFilter::setAlpha(float alpha)
{
    if (alpha <= 0.0f) {
        DBG("FILT", "WARNING: alpha %.3f clamped to 0.01", alpha);
        _alpha = 0.01f;
    } else if (alpha > 1.0f) {
        DBG("FILT", "WARNING: alpha %.3f clamped to 1.0", alpha);
        _alpha = 1.0f;
    } else {
        _alpha = alpha;
    }
}

// ============================================================
//  FREE FUNCTIONS — MAGNITUDE
// ============================================================

/**
 * @brief Compute Euclidean acceleration magnitude from three orthogonal axes.
 *
 *        Uses sqrtf() (single-precision) to avoid implicit double promotion
 *        on Xtensa-LX6/LX7.  The multiplications are slightly cheaper than
 *        powf() and produce identical results for squaring.
 *
 * @param ax  Accel X [g].
 * @param ay  Accel Y [g].
 * @param az  Accel Z [g].
 * @return float  sqrtf(ax² + ay² + az²) in g, always >= 0.
 */
float accelMagnitude(float ax, float ay, float az)
{
    return sqrtf(ax * ax + ay * ay + az * az);
}

/**
 * @brief Overload: compute magnitude directly from an IMUSample struct.
 *
 * @param s  IMUSample (ax, ay, az fields used; all others ignored).
 * @return float  Magnitude in g.
 */
float accelMagnitude(const IMUSample& s)
{
    return accelMagnitude(s.ax, s.ay, s.az);
}

// ============================================================
//  FREE FUNCTION — FILTERED MAGNITUDE
// ============================================================

/**
 * @brief Compute raw accel magnitude then push it through an EMAFilter.
 *
 *        This is the primary call used by the main loop and stroke detector.
 *        Separating raw-magnitude computation from filtering allows callers
 *        to retain the unfiltered value for Serial Plotter / logging before
 *        calling this function.
 *
 * @param filter  EMAFilter to update (state mutated in place).
 * @param ax      Accel X [g].
 * @param ay      Accel Y [g].
 * @param az      Accel Z [g].
 * @return float  Filtered magnitude in g.
 */
float filteredMagnitude(EMAFilter& filter, float ax, float ay, float az)
{
    float raw = accelMagnitude(ax, ay, az);
    return filter.update(raw);
}
