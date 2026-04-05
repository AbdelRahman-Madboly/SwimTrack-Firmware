/**
 * @file    mpu6500.h
 * @brief   IMU driver interface for SwimTrack — dev-board build.
 *
 *          Declares the IMUSample data structure and the MPU6500 driver class.
 *          Although the class is named MPU6500 for forward-compatibility with
 *          the target hardware, this build targets the MPU-6050 (GY-521 module).
 *
 *          Chip differences handled here:
 *            WHO_AM_I  : MPU-6050 → 0x68   (MPU-6500 → 0x70)
 *            Temp formula: raw/340.0+36.53  (MPU-6500: raw/333.87+21.0)
 *          All other registers, burst-read layout, and sensitivity values
 *          are identical between the two chips.
 *
 *          Hardware target : ESP32 30-pin dev board
 *          I2C pins        : SCL=GPIO22, SDA=GPIO21 (defined in config.h)
 *          I2C address     : 0x68 (AD0 floating — GY-521 has internal pull-down)
 *
 * @author  SwimTrack Firmware Team
 * @date    2025-01-01
 */

#pragma once

#include <Arduino.h>
#include <Wire.h>
#include "config.h"

// ============================================================
//  IMU DATA STRUCTURE
// ============================================================

/**
 * @brief One fully-converted IMU sample.
 *
 *  ax, ay, az  — linear acceleration [g]      (1 g ≈ 9.81 m/s²)
 *  gx, gy, gz  — angular velocity    [dps]    (degrees per second)
 *  temp_c      — die temperature      [°C]
 *  timestamp   — millis() at capture  [ms since boot]
 */
struct IMUSample {
    float    ax;         ///< Accel X  [g]
    float    ay;         ///< Accel Y  [g]
    float    az;         ///< Accel Z  [g]
    float    gx;         ///< Gyro  X  [dps]
    float    gy;         ///< Gyro  Y  [dps]
    float    gz;         ///< Gyro  Z  [dps]
    float    temp_c;     ///< Die temperature [°C]
    uint32_t timestamp;  ///< Capture time [ms]
};

// ============================================================
//  MPU6500 DRIVER CLASS
// ============================================================

/**
 * @brief I2C driver for the InvenSense MPU-6050 / MPU-6500 IMU.
 *
 *  Handles sensor wake-up, register configuration (range, sample-rate, DLPF),
 *  WHO_AM_I verification, and 14-byte burst reads converted to physical units.
 *
 * Typical usage:
 * @code
 *   MPU6500 imu;
 *   if (!imu.begin()) { // halt or blink error }
 *
 *   IMUSample s;
 *   if (imu.read(s)) {
 *       Serial.printf("az = %.3f g\n", s.az);
 *   }
 * @endcode
 */
class MPU6500 {
public:

    // --------------------------------------------------------
    //  Lifecycle
    // --------------------------------------------------------

    /**
     * @brief Default constructor.  Does not communicate with the sensor.
     */
    MPU6500();

    /**
     * @brief Initialise the IMU over I2C.
     *
     *  Sequence:
     *  1. Wire.begin(SDA, SCL) at I2C_CLOCK_HZ.
     *  2. Write PWR_MGMT_1 = 0x00 (wake from sleep); delay 100 ms.
     *  3. Write PWR_MGMT_1 = 0x01 (select PLL clock source).
     *  4. Read WHO_AM_I; verify == MPU_WHO_AM_I_VAL (0x68 for MPU-6050).
     *  5. Write CONFIG (0x1A) = 0x03 → DLPF_CFG=3, 44 Hz bandwidth.
     *  6. Write SMPLRT_DIV = (1000 / SAMPLE_RATE_HZ) - 1 = 19 → 50 Hz.
     *  7. Write GYRO_CONFIG with FS_SEL = GYRO_FS_SEL.
     *  8. Write ACCEL_CONFIG with AFS_SEL = ACCEL_FS_SEL.
     *
     * @return true  on success.
     * @return false if WHO_AM_I mismatches or any I2C write fails.
     */
    bool begin();

    // --------------------------------------------------------
    //  Data acquisition
    // --------------------------------------------------------

    /**
     * @brief Burst-read 14 bytes starting at register 0x3B and convert.
     *
     *  Byte order received (big-endian signed 16-bit pairs):
     *    [0-1]   AX_H, AX_L
     *    [2-3]   AY_H, AY_L
     *    [4-5]   AZ_H, AZ_L
     *    [6-7]   TEMP_H, TEMP_L
     *    [8-9]   GX_H, GX_L
     *    [10-11] GY_H, GY_L
     *    [12-13] GZ_H, GZ_L
     *
     *  Temperature formula (MPU-6050): °C = raw / 340.0 + 36.53
     *  Accel conversion: g  = raw / ACCEL_SENSITIVITY
     *  Gyro  conversion: dps = raw / GYRO_SENSITIVITY
     *
     * @param sample  Output: filled with converted values and millis() timestamp.
     * @return true on a successful 14-byte read, false on I2C error.
     */
    bool read(IMUSample& sample);

    // --------------------------------------------------------
    //  Status accessors
    // --------------------------------------------------------

    /**
     * @brief Return the cached WHO_AM_I value read during begin().
     * @return WHO_AM_I byte (0x68 for MPU-6050, 0x70 for MPU-6500).
     */
    uint8_t whoAmI() const;

    /**
     * @brief Return true if begin() completed successfully.
     * @return true if sensor is ready for read() calls.
     */
    bool isInitialised() const;

private:

    // --------------------------------------------------------
    //  Low-level I2C helpers
    // --------------------------------------------------------

    /**
     * @brief Write one byte to an IMU register.
     * @param reg    Register address.
     * @param value  Byte to write.
     * @return true on ACK, false on NACK / bus error.
     */
    bool writeRegister(uint8_t reg, uint8_t value);

    /**
     * @brief Read one byte from an IMU register.
     * @param reg    Register address.
     * @param value  Output: byte read from register.
     * @return true on success, false on I2C error or short read.
     */
    bool readRegister(uint8_t reg, uint8_t& value);

    // --------------------------------------------------------
    //  State
    // --------------------------------------------------------

    bool    _initialised;  ///< Set true after successful begin()
    uint8_t _whoAmI;       ///< Cached WHO_AM_I value
};
