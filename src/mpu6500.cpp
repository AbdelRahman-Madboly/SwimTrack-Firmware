/**
 * @file    mpu6500.cpp
 * @brief   IMU driver implementation for SwimTrack — dev-board build (MPU-6050).
 *
 *          Implements I2C initialisation, sensor configuration, and 14-byte
 *          burst-read with conversion to physical units (g, dps, °C).
 *
 *          MPU-6050 specifics applied here:
 *            WHO_AM_I expected : 0x68
 *            Temperature       : °C = raw / 340.0 + 36.53
 *
 *          Register map reference:
 *            "MPU-6000 and MPU-6050 Product Specification Rev 3.4"
 *            (InvenSense, 2013) — registers are identical to MPU-6500.
 *
 * @author  SwimTrack Firmware Team
 * @date    2025-01-01
 */

#include "mpu6500.h"

// ============================================================
//  CONSTRUCTOR
// ============================================================

/**
 * @brief Construct a new MPU6500 object.
 *        Sets internal state to uninitialised; no I2C communication occurs.
 */
MPU6500::MPU6500()
    : _initialised(false), _whoAmI(0x00)
{
}

// ============================================================
//  begin() — INITIALISE & CONFIGURE
// ============================================================

/**
 * @brief Initialise the MPU-6050 over I2C.
 *        Full sequence documented in mpu6500.h.
 *
 * @return true on success, false on WHO_AM_I mismatch or I2C error.
 */
bool MPU6500::begin()
{
    // 1. Start I2C bus with board-specific pins at 400 kHz
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(I2C_CLOCK_HZ);
    DBG("IMU", "I2C bus started — SDA=%d SCL=%d @ %lu Hz",
        PIN_I2C_SDA, PIN_I2C_SCL, I2C_CLOCK_HZ);

    // 2. Wake the sensor from its power-on sleep state
    if (!writeRegister(MPU_REG_PWR_MGMT_1, 0x00)) {
        DBG("IMU", "ERROR: PWR_MGMT_1 wake write failed (check wiring)");
        return false;
    }
    delay(100); // Let internal oscillator stabilise

    // 3. Switch to PLL gyro clock for better accuracy
    if (!writeRegister(MPU_REG_PWR_MGMT_1, 0x01)) {
        DBG("IMU", "ERROR: PWR_MGMT_1 PLL write failed");
        return false;
    }

    // 4. Verify device identity
    if (!readRegister(MPU_REG_WHO_AM_I, _whoAmI)) {
        DBG("IMU", "ERROR: WHO_AM_I read failed");
        return false;
    }
    DBG("IMU", "WHO_AM_I = 0x%02X (expected 0x%02X)",
        _whoAmI, MPU_WHO_AM_I_VAL);

    if (_whoAmI != MPU_WHO_AM_I_VAL) {
        DBG("IMU", "ERROR: WHO_AM_I mismatch — wrong chip or I2C address?");
        return false;
    }

    // 5. DLPF_CFG = 3 → accel BW=44 Hz, gyro BW=42 Hz, gyro ODR=1 kHz
    if (!writeRegister(MPU_REG_CONFIG, 0x03)) {
        DBG("IMU", "ERROR: CONFIG (DLPF) write failed");
        return false;
    }

    // 6. Sample rate divider — divides the 1 kHz gyro ODR
    //    Sample Rate = 1000 / (1 + SMPLRT_DIV)
    //    → SMPLRT_DIV = (1000 / 50) - 1 = 19
    const uint8_t smplrtDiv = static_cast<uint8_t>((1000u / SAMPLE_RATE_HZ) - 1u);
    if (!writeRegister(MPU_REG_SMPLRT_DIV, smplrtDiv)) {
        DBG("IMU", "ERROR: SMPLRT_DIV write failed");
        return false;
    }
    DBG("IMU", "SMPLRT_DIV=%d → %d Hz sample rate", smplrtDiv, SAMPLE_RATE_HZ);

    // 7. Gyro full-scale range: FS_SEL sits at bits [4:3]
    const uint8_t gyroConfig = static_cast<uint8_t>(GYRO_FS_SEL << 3);
    if (!writeRegister(MPU_REG_GYRO_CONFIG, gyroConfig)) {
        DBG("IMU", "ERROR: GYRO_CONFIG write failed");
        return false;
    }
    DBG("IMU", "Gyro  FS_SEL=%d → ±%d dps", GYRO_FS_SEL, 250 << GYRO_FS_SEL);

    // 8. Accel full-scale range: AFS_SEL sits at bits [4:3]
    const uint8_t accelConfig = static_cast<uint8_t>(ACCEL_FS_SEL << 3);
    if (!writeRegister(MPU_REG_ACCEL_CONFIG, accelConfig)) {
        DBG("IMU", "ERROR: ACCEL_CONFIG write failed");
        return false;
    }
    DBG("IMU", "Accel AFS_SEL=%d → ±%d g",  ACCEL_FS_SEL, 2 << ACCEL_FS_SEL);

    _initialised = true;
    DBG("IMU", "MPU-6050 ready. Accel=±%dg, Gyro=±%ddps, Rate=%dHz",
        (2 << ACCEL_FS_SEL), (250 << GYRO_FS_SEL), SAMPLE_RATE_HZ);
    return true;
}

// ============================================================
//  read() — BURST-READ 14 BYTES & CONVERT
// ============================================================

/**
 * @brief Burst-read one IMU sample (14 bytes) from register 0x3B.
 *
 *  Conversion factors used:
 *    Accel : g   = raw_int16 / ACCEL_SENSITIVITY   (4096.0 LSB/g at ±8g)
 *    Gyro  : dps = raw_int16 / GYRO_SENSITIVITY    (32.8 LSB/dps at ±1000dps)
 *    Temp  : °C  = raw_int16 / 340.0 + 36.53       (MPU-6050 formula)
 *
 * @param sample  Output struct populated on success.
 * @return true on a complete 14-byte read, false on any I2C error.
 */
bool MPU6500::read(IMUSample& sample)
{
    // Request 14 bytes starting at ACCEL_XOUT_H (0x3B)
    Wire.beginTransmission(MPU6500_I2C_ADDR);
    Wire.write(MPU_REG_ACCEL_XOUT_H);
    if (Wire.endTransmission(false) != 0) {   // false = repeated START
        DBG("IMU", "ERROR: I2C TX failed in read()");
        return false;
    }

    const uint8_t bytesExpected = 14;
    uint8_t received = Wire.requestFrom(
        static_cast<uint8_t>(MPU6500_I2C_ADDR), bytesExpected);

    if (received < bytesExpected) {
        DBG("IMU", "ERROR: short read — got %d of %d bytes", received, bytesExpected);
        // Drain any partial bytes to keep the bus clean
        while (Wire.available()) { Wire.read(); }
        return false;
    }

    // Read raw big-endian signed 16-bit values
    // Layout: AX AY AZ TEMP GX GY GZ  (each as H,L pair)
    auto readWord = [&]() -> int16_t {
        uint8_t hi = static_cast<uint8_t>(Wire.read());
        uint8_t lo = static_cast<uint8_t>(Wire.read());
        return static_cast<int16_t>((static_cast<uint16_t>(hi) << 8) | lo);
    };

    const int16_t rawAx   = readWord();
    const int16_t rawAy   = readWord();
    const int16_t rawAz   = readWord();
    const int16_t rawTemp = readWord();
    const int16_t rawGx   = readWord();
    const int16_t rawGy   = readWord();
    const int16_t rawGz   = readWord();

    // Convert to physical units
    sample.ax        = static_cast<float>(rawAx)   / ACCEL_SENSITIVITY;
    sample.ay        = static_cast<float>(rawAy)   / ACCEL_SENSITIVITY;
    sample.az        = static_cast<float>(rawAz)   / ACCEL_SENSITIVITY;
    sample.gx        = static_cast<float>(rawGx)   / GYRO_SENSITIVITY;
    sample.gy        = static_cast<float>(rawGy)   / GYRO_SENSITIVITY;
    sample.gz        = static_cast<float>(rawGz)   / GYRO_SENSITIVITY;

    // MPU-6500 temperature formula: °C = raw / 333.87 + 21.0
    sample.temp_c    = static_cast<float>(rawTemp) / 333.87f + 21.0f;

    sample.timestamp = millis();
    return true;
}


// Implementation continues in mpu6500_part2.cpp    