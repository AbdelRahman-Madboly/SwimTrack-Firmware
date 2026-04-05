/**
 * @file    lap_counter.h
 * @brief   Lap counting and rest detection for SwimTrack.
 *
 *          Lap / turn detection algorithm
 *          ───────────────────────────────
 *          A wall turn is identified by a two-phase confirmation sequence:
 *
 *          Phase 1 – Gyro spike  (SPIKE state)
 *            The absolute value of the gyro Z-axis reading exceeds
 *            TURN_GYRO_Z_THRESH_DPS (150 dps).  This catches the rapid
 *            rotation of a flip turn or open turn at the pool wall.
 *            The spike must last at least TURN_SPIKE_MIN_MS (30 ms) to
 *            reject brief noise transients.
 *
 *          Phase 2 – Glide confirmation  (GLIDE_WAIT state)
 *            Within TURN_GLIDE_WINDOW_MS (2000 ms) after the spike ends,
 *            the filtered accel magnitude must drop below GLIDE_ACCEL_THRESH_G
 *            (1.2 g) for at least TURN_GLIDE_MIN_MS (100 ms).  This confirms
 *            the swimmer has pushed off and is gliding — not just a mid-pool
 *            tumble or shake of the wrist.
 *
 *          Guard – minimum lap time
 *            A new lap is only registered if at least LAP_MIN_DURATION_MS
 *            (15 000 ms) have elapsed since the previous lap started.
 *            This prevents double-counting from large oscillations or noise.
 *
 *          Rest detection
 *          ──────────────
 *          A 1-second rolling window (SAMPLE_RATE_HZ samples) accumulates
 *          filtered accel magnitude values.  Variance is computed from that
 *          window.  If variance remains below REST_VARIANCE_THRESH (0.05 g²)
 *          for REST_DURATION_MS (5 000 ms) continuously, rest is declared.
 *          Rest ends as soon as variance exceeds the threshold again.
 *
 *          Lap data
 *          ─────────
 *          Each completed lap is described by a LapRecord struct:
 *            lap_number, start_time_ms, end_time_ms,
 *            stroke_count, swolf, stroke_rate_spm
 *
 * @author  SwimTrack Firmware Team
 * @date    2025-01-01
 */

#pragma once

#include <Arduino.h>
#include "config.h"

// ============================================================
//  LOCAL CONSTANTS  (override in config.h with your own #define)
// ============================================================

/**
 * @brief Minimum duration (ms) the gyro Z spike must be sustained
 *        to be treated as a real turn rather than vibration noise.
 */
#ifndef TURN_SPIKE_MIN_MS
  #define TURN_SPIKE_MIN_MS    30
#endif

/**
 * @brief Minimum duration (ms) that accel magnitude must stay below
 *        GLIDE_ACCEL_THRESH_G to confirm the glide phase after a turn.
 */
#ifndef TURN_GLIDE_MIN_MS
  #define TURN_GLIDE_MIN_MS    100
#endif

/**
 * @brief Size of the variance window in samples.
 *        At 50 Hz this covers exactly 1 second of data.
 */
#ifndef VAR_WINDOW_SIZE
  #define VAR_WINDOW_SIZE      50
#endif

// ============================================================
//  LAP RECORD STRUCT
// ============================================================

/**
 * @brief Data recorded for one completed lap.
 *
 *  lap_number      – 1-based lap index within the current session.
 *  start_time_ms   – millis() when this lap started (= previous turn end).
 *  end_time_ms     – millis() when this lap ended (= this turn confirmed).
 *  duration_s      – (end - start) / 1000.0  [seconds].
 *  stroke_count    – number of strokes counted during this lap.
 *  swolf           – stroke_count + duration_s  (lower = more efficient).
 *  stroke_rate_spm – average strokes per minute for this lap.
 */
struct LapRecord {
    uint16_t lap_number;
    uint32_t start_time_ms;
    uint32_t end_time_ms;
    float    duration_s;
    uint32_t stroke_count;
    float    swolf;
    float    stroke_rate_spm;
    float    dps_m_per_stroke;   ///< Distance Per Stroke [m/stroke] = pool_length_m / stroke_count
};

// ============================================================
//  LAP COUNTER CLASS
// ============================================================

/**
 * @brief Detects pool turns and counts laps; also monitors for rest.
 *
 * The caller must call update() once per IMU sample, passing in:
 *   - the raw (unfiltered) gyro Z reading in dps
 *   - the already-filtered accel magnitude from imu_filters
 *   - the current stroke count from StrokeDetector (accumulated this lap)
 *   - the current stroke rate from StrokeDetector
 *   - the current millis() timestamp
 *
 * Usage:
 * @code
 *   LapCounter laps;
 *   laps.begin();
 *
 *   // In sample loop:
 *   bool newLap  = laps.update(gyroZ, filtMag, strokeCount, rateSpm, millis());
 *   bool resting = laps.isResting();
 * @endcode
 */
class LapCounter {
public:

    // --------------------------------------------------------
    //  Lifecycle
    // --------------------------------------------------------

    /** @brief Default constructor. Call begin() before use. */
    LapCounter();

    /**
     * @brief Initialise (or re-initialise) the lap counter.
     *        Resets all state: lap count, turn FSM, rest FSM, variance window.
     *        Call at session start.
     */
    void begin();

    // --------------------------------------------------------
    //  Core update — call once per IMU sample
    // --------------------------------------------------------

    /**
     * @brief Process one IMU sample through the turn and rest detectors.
     *
     * @param gyroZ_dps     Raw gyro Z reading [dps] — NOT filtered.
     *                      Using raw data catches the sharp spike peak that
     *                      a low-pass filter would attenuate.
     * @param filtMag       EMA-filtered accel magnitude [g].
     * @param lapStrokes    Stroke count accumulated since the last turn
     *                      (caller should reset their own counter after a lap).
     * @param rateSpm       Current stroke rate [spm] from StrokeDetector.
     * @param nowMs         Current millis() timestamp.
     * @return true         if a new lap was just registered on this call.
     * @return false        otherwise.
     */
    bool update(float gyroZ_dps, float filtMag,
                uint32_t lapStrokes, float rateSpm,
                uint32_t nowMs,
                uint8_t poolLengthM = DEFAULT_POOL_LENGTH_M);

    // --------------------------------------------------------
    //  Lap accessors
    // --------------------------------------------------------

    /**
     * @brief Total completed laps since begin().
     * @return uint16_t  Lap count (0 until first turn is confirmed).
     */
    uint16_t lapCount() const { return _lapCount; }

    /**
     * @brief Data record for the most recently completed lap.
     *        Only valid when lapCount() > 0.
     * @return const LapRecord&  Reference to last lap data.
     */
    const LapRecord& lastLap() const { return _lastLap; }

    /**
     * @brief millis() when the current (in-progress) lap started.
     *        Equals 0 before the first turn, otherwise the millis() of the
     *        previous confirmed turn.
     * @return uint32_t
     */
    uint32_t lapStartMs() const { return _lapStartMs; }

    /**
     * @brief Elapsed time of the current in-progress lap [seconds].
     *        Computed from lapStartMs() to nowMs passed to the last update().
     * @return float  Seconds since the last confirmed turn (or session start).
     */
    float currentLapElapsedS() const { return _currentLapElapsedS; }

    /**
     * @brief True while the turn FSM is in the SPIKE or GLIDE_WAIT state.
     *        Useful for Serial Plotter visualisation.
     * @return bool
     */
    bool turnPending() const { return _turnPending; }

    // --------------------------------------------------------
    //  Rest accessors
    // --------------------------------------------------------

    /**
     * @brief True while the swimmer is classified as resting.
     *        Set when variance is below REST_VARIANCE_THRESH for REST_DURATION_MS.
     *        Cleared as soon as variance exceeds the threshold.
     * @return bool
     */
    bool isResting() const { return _resting; }

    /**
     * @brief True on the exact update() call when rest began.
     *        Caller should consume this (it is cleared next call).
     */
    bool restJustStarted() const { return _restJustStarted; }

    /**
     * @brief True on the exact update() call when rest ended.
     *        Caller should consume this (it is cleared next call).
     */
    bool restJustEnded() const { return _restJustEnded; }

    /**
     * @brief Duration of the most recently completed rest period [seconds].
     *        Only meaningful after restJustEnded() returns true.
     */
    float lastRestDurationS() const { return _lastRestDurationS; }

    /**
     * @brief Current rolling variance of filtered accel magnitude [g²].
     *        Useful for Serial Plotter and threshold tuning.
     */
    float currentVariance() const { return _variance; }

    // --------------------------------------------------------
    //  Control
    // --------------------------------------------------------

    /**
     * @brief Full reset — identical to begin().
     *        Resets lap count, last lap record, and all FSM state.
     */
    void reset();

private:

    // ── Turn detection FSM ──────────────────────────────────────
    /**
     * @brief Turn detection states.
     *   IDLE       – no turn in progress
     *   SPIKE      – gyro Z exceeded threshold; waiting for spike to settle
     *   GLIDE_WAIT – spike ended; waiting for accel glide to confirm
     */
    enum class TurnState : uint8_t { IDLE, SPIKE, GLIDE_WAIT };

    TurnState _turnState;       ///< Current FSM state
    bool      _turnPending;     ///< True while not in IDLE
    uint32_t  _spikeStartMs;    ///< millis() when spike was first detected
    uint32_t  _spikeEndMs;      ///< millis() when spike dropped below threshold
    uint32_t  _glideStartMs;    ///< millis() when glide phase began
    float     _peakGyroZ;       ///< Largest |gyroZ| seen during current spike

    // ── Lap tracking ────────────────────────────────────────────
    uint16_t  _lapCount;        ///< Completed laps since begin()
    uint32_t  _lapStartMs;      ///< millis() when current lap started
    float     _currentLapElapsedS; ///< Cached elapsed time updated each call
    LapRecord _lastLap;         ///< Record of the most recently completed lap

    // ── Rest detection ──────────────────────────────────────────
    /**
     * @brief Circular buffer holding the last VAR_WINDOW_SIZE filtered-mag
     *        samples for variance computation.
     */
    float    _varWindow[VAR_WINDOW_SIZE];
    uint8_t  _varHead;          ///< Next write index in _varWindow[]
    bool     _varWindowFull;    ///< True once all VAR_WINDOW_SIZE slots filled
    float    _variance;         ///< Variance computed from the window [g²]

    bool     _resting;          ///< Currently in rest state
    bool     _restJustStarted;  ///< Pulse-true on rest entry
    bool     _restJustEnded;    ///< Pulse-true on rest exit
    uint32_t _lowVarStartMs;    ///< millis() when variance first went low
    uint32_t _restStartMs;      ///< millis() when rest state was entered
    float    _lastRestDurationS;///< Duration of last completed rest [s]

    // ── Private helpers ─────────────────────────────────────────

    /**
     * @brief Confirm a completed turn: register the lap, emit debug log,
     *        update _lapCount, save _lastLap, and reset the turn FSM.
     *
     * @param lapStrokes  Stroke count for this lap.
     * @param rateSpm     Stroke rate for this lap.
     * @param nowMs       Timestamp of confirmation.
     */
    void _confirmLap(uint32_t lapStrokes, float rateSpm, uint32_t nowMs,
                     uint8_t poolLengthM = DEFAULT_POOL_LENGTH_M);

    /**
     * @brief Push one magnitude sample into the variance window and recompute
     *        the running variance using Welford's online algorithm.
     *        Updates _variance.
     *
     * @param mag  Filtered accel magnitude sample [g].
     */
    void _updateVariance(float mag);

    /**
     * @brief Evaluate whether rest state should start or end based on
     *        the current _variance value.  Updates _resting and the
     *        _restJustStarted / _restJustEnded pulse flags.
     *
     * @param nowMs  Current millis() timestamp.
     */
    void _updateRest(uint32_t nowMs);
};