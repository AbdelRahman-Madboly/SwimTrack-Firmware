/**
 * @file    mpu6500_part2.cpp
 * @brief   MPU-6050 driver — status accessors and I2C register helpers.
 *
 *          Continuation of mpu6500.cpp.  Split to keep each file under
 *          200 lines per SwimTrack code standards.
 *
 *          Contains:
 *            - whoAmI()         — return cached WHO_AM_I byte
 *            - isInitialised()  — return init flag
 *            - writeRegister()  — single-byte I2C write
 *            - readRegister()   — single-byte I2C read
 *
 * @author  SwimTrack Firmware Team
 * @date    2025-01-01
 */

#include "mpu6500.h"

// ============================================================
//  STATUS ACCESSORS
// ============================================================

/**
 * @brief Return the cached WHO_AM_I value read during begin().
 * @return WHO_AM_I byte (0x68 for MPU-6050, 0x70 for MPU-6500).
 */
uint8_t MPU6500::whoAmI() const
{
    return _whoAmI;
}

/**
 * @brief Return true if begin() completed successfully.
 * @return true if the sensor is ready to accept read() calls.
 */
bool MPU6500::isInitialised() const
{
    return _initialised;
}

// ============================================================
//  PRIVATE — LOW-LEVEL I2C HELPERS
// ============================================================

/**
 * @brief Write one byte to an IMU register via I2C.
 *
 * @param reg    Register address to write.
 * @param value  Byte value to send.
 * @return true if endTransmission() returns 0 (ACK received).
 * @return false on NACK, timeout, or other bus error.
 */
bool MPU6500::writeRegister(uint8_t reg, uint8_t value)
{
    Wire.beginTransmission(MPU6500_I2C_ADDR);
    Wire.write(reg);
    Wire.write(value);
    return (Wire.endTransmission(true) == 0);
}

/**
 * @brief Read one byte from an IMU register via I2C.
 *
 *  Uses a repeated-START sequence:
 *    TX: START | addr+W | reg | REPEATED_START | addr+R | 1 byte | STOP
 *
 * @param reg    Register address to read.
 * @param value  Output: byte received from the sensor.
 * @return true on success (1 byte received).
 * @return false on NACK, timeout, or short read.
 */
bool MPU6500::readRegister(uint8_t reg, uint8_t& value)
{
    Wire.beginTransmission(MPU6500_I2C_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) {   // false = repeated START
        return false;
    }
    const uint8_t received = Wire.requestFrom(
        static_cast<uint8_t>(MPU6500_I2C_ADDR),
        static_cast<uint8_t>(1));
    if (received < 1) {
        return false;
    }
    value = static_cast<uint8_t>(Wire.read());
    return true;
}
