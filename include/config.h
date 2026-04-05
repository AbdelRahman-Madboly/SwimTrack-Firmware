/**
 * @file    config.h
 * @brief   SwimTrack firmware — master configuration header.
 *
 *          Target hardware  : LOLIN S2 Mini (ESP32-S2) + GY-6500 (MPU-6500)
 *          I2C              : SCL=GPIO33, SDA=GPIO34
 *
 *          Changes from dev-board (3 values only — all names preserved):
 *            PIN_I2C_SCL     : 22  → 33
 *            PIN_I2C_SDA     : 21  → 34
 *            PIN_LED         :  2  → 15
 *            MPU_WHO_AM_I_VAL: 0x68 → 0x70
 *
 * @author  SwimTrack Firmware Team
 * @date    2026-03-30
 */

#pragma once

#include <Arduino.h>

// ============================================================
//  DEBUG OUTPUT
// ============================================================

#define DEBUG_SERIAL 1

#if DEBUG_SERIAL
  #define DBG(tag, fmt, ...) Serial.printf("[" tag "] " fmt "\n", ##__VA_ARGS__)
#else
  #define DBG(tag, fmt, ...) do {} while(0)
#endif

// ============================================================
//  PIN ASSIGNMENTS  — LOLIN S2 Mini
// ============================================================

#define PIN_I2C_SCL       33    ///< I2C clock -> GY-6500 SCL   (was 22)
#define PIN_I2C_SDA       34    ///< I2C data  -> GY-6500 SDA   (was 21)
#define PIN_LED           15    ///< S2 Mini built-in LED        (was 2)
#define PIN_BATTERY_ADC    1    ///< ADC - battery voltage divider
#define PIN_BUTTON         0    ///< BOOT button (active LOW, internal pull-up)

// ============================================================
//  I2C / MPU INTERFACE
// ============================================================

#define I2C_CLOCK_HZ      400000UL

#define MPU6500_I2C_ADDR  0x68

#define MPU_WHO_AM_I_VAL  0x70   ///< CHANGED: MPU-6500 (was 0x68 for MPU-6050)

// ============================================================
//  MPU REGISTER MAP
// ============================================================

#define MPU_REG_SMPLRT_DIV    0x19
#define MPU_REG_CONFIG        0x1A
#define MPU_REG_GYRO_CONFIG   0x1B
#define MPU_REG_ACCEL_CONFIG  0x1C
#define MPU_REG_ACCEL_XOUT_H  0x3B
#define MPU_REG_TEMP_OUT_H    0x41
#define MPU_REG_WHO_AM_I      0x75
#define MPU_REG_PWR_MGMT_1    0x6B

// ============================================================
//  IMU RANGE SETTINGS
// ============================================================

#define ACCEL_FS_SEL      2
#define ACCEL_SENSITIVITY 4096.0f
#define GYRO_FS_SEL       2
#define GYRO_SENSITIVITY  32.8f

// ============================================================
//  SAMPLING
// ============================================================

#define SAMPLE_RATE_HZ    50
#define SAMPLE_PERIOD_MS  (1000UL / SAMPLE_RATE_HZ)

// ============================================================
//  EMA FILTER
// ============================================================

#define EMA_ALPHA         0.3f

// ============================================================
//  STROKE DETECTION
// ============================================================

#define STROKE_THRESHOLD_G    0.4f
#define STROKE_MIN_GAP_MS     500UL

// ============================================================
//  LAP / TURN DETECTION
// ============================================================

#define TURN_GYRO_Z_THRESH_DPS  150.0f
#define TURN_GLIDE_WINDOW_MS    2000
#define GLIDE_ACCEL_THRESH_G    1.2f
#define LAP_MIN_DURATION_MS     5000

// ============================================================
//  REST DETECTION
// ============================================================

#define REST_VARIANCE_THRESH    0.05f
#define REST_DURATION_MS        5000

// ============================================================
//  SESSION STORAGE
// ============================================================

#define SESSION_DIR             "/sessions"
#define SESSION_MAX_LAPS        80
#define SESSION_MAX_RESTS       20
#define SESSION_JSON_CAPACITY   20480
#define DEFAULT_POOL_LENGTH_M   25

// ============================================================
//  WIFI ACCESS POINT  (names unchanged - wifi_server.cpp uses these)
// ============================================================

#define WIFI_AP_SSID    "SwimTrack"
#define WIFI_AP_PASS    "swim1234"
#define WIFI_AP_IP      "192.168.4.1"

// ============================================================
//  BATTERY / ADC
// ============================================================

#define BATT_DIVIDER_RATIO  0.5f
#define ADC_VREF_MV         3300
#define ADC_MAX_COUNT       4095
#define BATT_FULL_MV        4200
#define BATT_EMPTY_MV       3300
#define BATT_SLEEP_MV       3400