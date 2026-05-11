// =============================================================================
// SoleSense v0.2 -- config.h
// All compile-time constants live here.
// =============================================================================

#pragma once

#include <Arduino.h>

// -- Identity -----------------------------------------------------------------
constexpr const char* SS_FIRMWARE_VERSION = "v0.2-dev";
constexpr const char* SS_BOARD_NAME       = "XIAO ESP32-C3";

// -- WiFi AP ------------------------------------------------------------------
constexpr const char* SS_AP_SSID = "SoleSense";
constexpr const char* SS_AP_PASS = "solesense";

// -- Pin map (per hardware/electricalpins.pdf) --------------------------------
// XIAO ESP32-C3 Seeed-name -> GPIO mapping for reference:
//   D0..D2 (A0..A2) = GPIO2/3/4    -- shared FSR ADCs
//   D4              = GPIO6        -- I2C SDA
//   D5              = GPIO7        -- I2C SCL
//   D6              = GPIO21       -- IMU INT
//   D7              = GPIO20       -- FSR power set 1
//   D8              = GPIO8        -- FSR power set 2
//   D9              = GPIO9        -- on-board BOOT button (wake)
constexpr uint8_t PIN_SDA       = 6;    // D4  - I2C SDA  -> MPU-6050 SDA
constexpr uint8_t PIN_SCL       = 7;    // D5  - I2C SCL  -> MPU-6050 SCL
constexpr uint8_t PIN_IMU_INT   = 21;   // D6  - MPU-6050 INT (reserved; unused today)
constexpr uint8_t PIN_ADC_A     = 2;    // D0  - shared analog A (FSR 1A / 2A)
constexpr uint8_t PIN_ADC_B     = 3;    // D1  - shared analog B (FSR 1B / 2B)
constexpr uint8_t PIN_ADC_C     = 4;    // D2  - shared analog C (FSR 1C / 2C)
constexpr uint8_t PIN_PWR_SET1  = 20;   // D7  - FSR Set 1 power (1A/1B/1C)
constexpr uint8_t PIN_PWR_SET2  = 8;    // D8  - FSR Set 2 power (2A/2B/2C)
constexpr uint8_t PIN_WAKE      = 9;    // D9  - on-board BOOT button

// -- MPU-6050 -----------------------------------------------------------------
constexpr uint8_t MPU6050_ADDR     = 0x68;
constexpr float   ACCEL_LSB_PER_G  = 16384.0f;
constexpr float   G_TO_MS2         = 9.80665f;
constexpr float   GYRO_LSB_PER_DPS = 131.0f;

// IMU sensor-fusion: accel_z stddev required to consider the IMU "connected"
// (a disconnected MPU leaves gAccel pinned, so stddev sits at exactly 0).
constexpr float IMU_CONNECTED_STDDEV_FLOOR = 0.05f;
// |accel_z - running_mean| above this many m/s^2 is registered as an impact.
constexpr float IMU_IMPACT_DELTA_MS2       = 8.0f;

// -- Sampling -----------------------------------------------------------------
// 500 Hz: chosen to oversample running-impact rising edges (~5-20 ms wide) by
// 5-10x, so FSR-jerk extrapolation has enough resolution for honest loading-
// rate math. Per-sample budget at this rate: ~2 ms; measured ~600 us (matrix
// scan + I^2C IMU + Goertzel + outlier check), leaves ~1.4 ms slack. RAM is
// rate-independent because the Goertzel FFT is incremental (no growing sample
// buffer). Hard ceiling on this MCU/sensor stack is ~1.5 kHz; 500 is the
// comfortable working point.
constexpr uint32_t SAMPLE_RATE_HZ      = 500;
constexpr uint32_t SAMPLE_PERIOD_US    = 1000000UL / SAMPLE_RATE_HZ;
constexpr uint8_t  N_FSR               = 6;
constexpr uint8_t  N_IMU_AXES          = 6;           // ax, ay, az, gx, gy, gz
constexpr uint8_t  N_CHANNELS_TOTAL    = N_FSR + N_IMU_AXES;

// -- FFT (Goertzel bins) ------------------------------------------------------
// See fft.h for the actual frequency table. Sized for ~9KB of state.
constexpr uint8_t  FFT_BINS_PER_CHAN   = 18;
constexpr uint16_t FFT_TOTAL_BINS      = N_CHANNELS_TOTAL * FFT_BINS_PER_CHAN;

// -- Outlier buffer -----------------------------------------------------------
constexpr uint8_t  OUTLIER_CAPACITY    = 100;
constexpr float    OUTLIER_SIGMA_THRESH = 3.0f;       // > N stddev from running mean

// -- Storage (multi-slot ring buffer) -----------------------------------------
constexpr uint8_t  STORAGE_SLOT_COUNT      = 10;
constexpr uint32_t STORAGE_SLOT_SIZE_BYTES = 10240;   // 10 KB per slot, 100 KB total
constexpr const char* STORAGE_FILENAME     = "/run.bin";
constexpr uint32_t STORAGE_FLUSH_MS        = 3000;    // flush every 3 seconds
constexpr uint32_t STORAGE_MAGIC_HEADER    = 0x55EAB001;
constexpr uint32_t STORAGE_MAGIC_TRAILER   = 0xC0DEF00D;

// -- HTTP server --------------------------------------------------------------
constexpr uint16_t HTTP_PORT = 80;
