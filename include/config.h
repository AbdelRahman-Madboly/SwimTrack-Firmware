/**
 * @file    config.h
 * @brief   SwimTrack firmware — master configuration header.
 *
 *          Target hardware  : LOLIN S2 Mini (ESP32-S2) + GY-6500 (MPU-6500)
 *          I2C              : SCL=GPIO33, SDA=GPIO34
 *
 *          v2 changes (data-driven threshold update):
 *            STROKE_THRESHOLD_G   : 0.4f   → 0.0950f  (from dry-land analysis)
 *            REST_VARIANCE_THRESH : 0.05f  → 0.16421f (from dry-land analysis)
 *            LAP_MIN_DURATION_MS  : 5000   → 15000    (pool value, not bench)
 *
 * @author  SwimTrack Firmware Team
 * @date    2026-04
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

#define PIN_I2C_SCL       33    ///< I2C clock -> GY-6500 SCL
#define PIN_I2C_SDA       34    ///< I2C data  -> GY-6500 SDA
#define PIN_LED           15    ///< S2 Mini built-in LED
#define PIN_BATTERY_ADC    1    ///< ADC - battery voltage divider (GPIO1)
#define PIN_BUTTON         0    ///< BOOT button (active LOW, internal pull-up)

// ============================================================
//  I2C / MPU INTERFACE
// ============================================================

#define I2C_CLOCK_HZ      400000UL

#define MPU6500_I2C_ADDR  0x68

#define MPU_WHO_AM_I_VAL  0x70   ///< MPU-6500 expected value (NOT 0x68 for MPU-6050)

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
//  ±8g  @ 4096 LSB/g   (AFS_SEL = 2)
//  ±1000dps @ 32.8 LSB/dps (FS_SEL = 2)
// ============================================================

#define ACCEL_FS_SEL        2
#define ACCEL_SENSITIVITY   4096.0f   ///< LSB per g at ±8g
#define GYRO_FS_SEL         2
#define GYRO_SENSITIVITY    32.8f     ///< LSB per dps at ±1000dps

// ============================================================
//  SAMPLING
// ============================================================

#define SAMPLE_RATE_HZ    50
#define SAMPLE_PERIOD_MS  (1000UL / SAMPLE_RATE_HZ)   ///< 20 ms

// ============================================================
//  EMA FILTER
// ============================================================

#define EMA_ALPHA         0.3f    ///< Smoothing factor for mag filter

// ============================================================
//  STROKE DETECTION
//  STROKE_THRESHOLD_G updated from 0.4f → 0.0950f (dry-land data analysis)
// ============================================================

#define STROKE_THRESHOLD_G        0.0950f   ///< Dynamic threshold above baseline [g]
#define STROKE_MIN_GAP_MS         500UL     ///< Minimum ms between strokes (anti-double-count)

// ============================================================
//  LAP / TURN DETECTION
//  LAP_MIN_DURATION_MS updated from 5000 → 15000 (pool value)
// ============================================================

#define TURN_GYRO_Z_THRESH_DPS    150.0f   ///< gyroZ spike threshold for turn detection [dps]
#define TURN_GLIDE_WINDOW_MS      2000     ///< Window after turn to detect glide phase [ms]
#define GLIDE_ACCEL_THRESH_G      1.2f     ///< Mag below this = gliding (post-push-off) [g]
#define LAP_MIN_DURATION_MS       15000    ///< Minimum lap duration [ms] — pool 25m value

// ============================================================
//  REST DETECTION
//  REST_VARIANCE_THRESH updated from 0.05f → 0.16421f (dry-land data analysis)
// ============================================================

#define REST_VARIANCE_THRESH      0.16421f  ///< mag_filt variance below this = resting [g²]
#define REST_DURATION_MS          5000      ///< Must be below threshold for this long [ms]

// ============================================================
//  SESSION STORAGE
// ============================================================

#define SESSION_DIR             "/sessions"
#define SESSION_MAX_LAPS        80
#define SESSION_MAX_RESTS       20
#define SESSION_JSON_CAPACITY   20480
#define DEFAULT_POOL_LENGTH_M   25

// ============================================================
//  WIFI ACCESS POINT
// ============================================================

#define WIFI_AP_SSID    "SwimTrack"
#define WIFI_AP_PASS    "swim1234"
#define WIFI_AP_IP      "192.168.4.1"

// ============================================================
//  BATTERY / ADC
//  100k + 100k voltage divider on PIN_BATTERY_ADC (GPIO1).
//  BATT_DIVIDER_RATIO = lower_R / (upper_R + lower_R) = 0.5
// ============================================================

#define BATT_DIVIDER_RATIO  0.5f     ///< Voltage divider ratio
#define ADC_VREF_MV         3300     ///< ESP32-S2 ADC reference voltage [mV]
#define ADC_MAX_COUNT       4095     ///< 12-bit ADC full scale
#define BATT_FULL_MV        4200     ///< Fully charged LiPo [mV]
#define BATT_EMPTY_MV       3300     ///< Empty / cutoff voltage [mV]
#define BATT_SLEEP_MV       3400     ///< Voltage below which to enter deep sleep [mV]

// ============================================================
//  SLEEP TIMEOUT
// ============================================================

#define SLEEP_TIMEOUT_MS    (5UL * 60UL * 1000UL)  ///< 5 min no client → deep sleep