// =============================================================================
// SoleSense v0.2 — sensors.h
// Sensor reads, lifted from v0.1 with minor module-isation. The matrix-scan
// scheme (2 sets of 3 FSRs, 3 shared ADC pins) is unchanged.
// =============================================================================

#pragma once

#include <Arduino.h>
#include "config.h"

// Latest sensor reading globals (updated by sensors_read_all()).
//   gFsr[0..5] in raw ADC counts (0..4095) with zero-offset applied
//   gAccel[0..2] in m/s², offset applied
//   gGyro[0..2] in °/s, offset applied
extern int16_t gFsr[N_FSR];
extern float   gAccel[3];
extern float   gGyro[3];

// Calibration offsets (loaded from NVS at boot, written by calibrate_*).
extern int   gFsrZero[N_FSR];
extern float gImuOffset[6];   // ax, ay, az, gx, gy, gz

// One-time hardware init: pin modes, ADC resolution, I2C, MPU-6050 wake.
void sensors_init();

// Read all 6 FSRs (set-scan) + IMU. Populates the globals above.
void sensors_read_all();

// Run an FSR zero calibration: 32 samples averaged with insole unloaded,
// store into gFsrZero[]. Caller is responsible for persisting to NVS.
void sensors_calibrate_fsr();

// Run an IMU zero calibration: 64 samples averaged with insole flat,
// store into gImuOffset[] (gravity stays on accel-Z).
void sensors_calibrate_imu();
