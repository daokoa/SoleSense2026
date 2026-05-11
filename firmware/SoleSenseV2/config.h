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

// -- Pin map (matches v0.1; lifted unchanged) ---------------------------------
constexpr uint8_t PIN_SDA       = 6;    // I2C - MPU-6050
constexpr uint8_t PIN_SCL       = 7;
constexpr uint8_t PIN_ADC_A     = 2;    // GPIO2 / A0 - shared analog A (FSR 1A and 2A)
constexpr uint8_t PIN_ADC_B     = 3;    // GPIO3      - shared analog B (FSR 1B and 2B)
constexpr uint8_t PIN_ADC_C     = 4;    // GPIO4      - shared analog C (FSR 1C and 2C)
constexpr uint8_t PIN_PWR_SET1  = 5;    // GPIO5  - digital power for Set 1 (1A, 1B, 1C)
constexpr uint8_t PIN_PWR_SET2  = 10;   // GPIO10 - digital power for Set 2 (2A, 2B, 2C)
constexpr uint8_t PIN_WAKE      = 9;    // GPIO9 - on-board BOOT button

// -- MPU-6050 -----------------------------------------------------------------
constexpr uint8_t MPU6050_ADDR     = 0x68;
constexpr float   ACCEL_LSB_PER_G  = 16384.0f;
constexpr float   G_TO_MS2         = 9.80665f;
constexpr float   GYRO_LSB_PER_DPS = 131.0f;

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
