// =============================================================================
// SoleSense v0.2 — sensors.cpp
// =============================================================================

#include "sensors.h"

#include <Wire.h>

int16_t gFsr[N_FSR]      = {0,0,0,0,0,0};
float   gAccel[3]        = {0,0,0};
float   gGyro[3]         = {0,0,0};
int     gFsrZero[N_FSR]  = {0,0,0,0,0,0};
float   gImuOffset[6]    = {0,0,0,0,0,0};

// Matrix-scan crosstalk fix: drive the INACTIVE set's power pin LOW (not
// floating). Pressing an FSR raises voltage at its ADC pin; with the other
// set's power pin floating, that voltage leaks back through any FSR in the
// inactive set, raising the floating power pin's voltage and propagating
// onto the other two ADC pins (classic "ghosting"). Driving the inactive
// pin LOW shorts that ghost path to ground so each set is read cleanly.
//
// Settling time bumped to 150 µs because the LOW-driving means a real
// transition to settle each scan, where the previous floating-pin scheme
// settled almost instantly. Total per-sample budget at 500 Hz is 2 ms;
// 6 reads × ~170 µs = ~1 ms, comfortably within budget.
static void read_set_a() {
  pinMode(PIN_PWR_SET2, OUTPUT);
  digitalWrite(PIN_PWR_SET2, LOW);
  pinMode(PIN_PWR_SET1, OUTPUT);
  digitalWrite(PIN_PWR_SET1, HIGH);
  delayMicroseconds(150);
  gFsr[0] = analogRead(PIN_ADC_A) - gFsrZero[0];   // Heel medial
  gFsr[1] = analogRead(PIN_ADC_B) - gFsrZero[1];   // Heel lateral
  gFsr[2] = analogRead(PIN_ADC_C) - gFsrZero[2];   // Midfoot medial
}

static void read_set_b() {
  pinMode(PIN_PWR_SET1, OUTPUT);
  digitalWrite(PIN_PWR_SET1, LOW);
  pinMode(PIN_PWR_SET2, OUTPUT);
  digitalWrite(PIN_PWR_SET2, HIGH);
  delayMicroseconds(150);
  gFsr[3] = analogRead(PIN_ADC_A) - gFsrZero[3];   // Midfoot lateral
  gFsr[4] = analogRead(PIN_ADC_B) - gFsrZero[4];   // Forefoot medial
  gFsr[5] = analogRead(PIN_ADC_C) - gFsrZero[5];   // Forefoot lateral
}

// Park: drive both sets LOW (instead of high-Z) when not actively reading,
// so any leakage path is grounded rather than floating.
static void park_sets_high_z() {
  pinMode(PIN_PWR_SET1, OUTPUT);
  pinMode(PIN_PWR_SET2, OUTPUT);
  digitalWrite(PIN_PWR_SET1, LOW);
  digitalWrite(PIN_PWR_SET2, LOW);
}

static void read_imu() {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x3B);                               // ACCEL_XOUT_H
  Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)MPU6050_ADDR, (uint8_t)14);

  int16_t ax = (Wire.read() << 8) | Wire.read();
  int16_t ay = (Wire.read() << 8) | Wire.read();
  int16_t az = (Wire.read() << 8) | Wire.read();
  Wire.read(); Wire.read();                       // discard temp
  int16_t gx = (Wire.read() << 8) | Wire.read();
  int16_t gy = (Wire.read() << 8) | Wire.read();
  int16_t gz = (Wire.read() << 8) | Wire.read();

  gAccel[0] = (ax / ACCEL_LSB_PER_G) * G_TO_MS2 - gImuOffset[0];
  gAccel[1] = (ay / ACCEL_LSB_PER_G) * G_TO_MS2 - gImuOffset[1];
  gAccel[2] = (az / ACCEL_LSB_PER_G) * G_TO_MS2 - gImuOffset[2];
  gGyro[0]  = gx / GYRO_LSB_PER_DPS - gImuOffset[3];
  gGyro[1]  = gy / GYRO_LSB_PER_DPS - gImuOffset[4];
  gGyro[2]  = gz / GYRO_LSB_PER_DPS - gImuOffset[5];
}

void sensors_init() {
  pinMode(PIN_PWR_SET1, INPUT);
  pinMode(PIN_PWR_SET2, INPUT);
  pinMode(PIN_ADC_A,    INPUT);
  pinMode(PIN_ADC_B,    INPUT);
  pinMode(PIN_ADC_C,    INPUT);
  analogReadResolution(12);

  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(400000);

  // MPU-6050 wake + default ranges (±2g, ±250°/s).
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x6B); Wire.write(0x00);
  Wire.endTransmission();
  delay(10);
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x1B); Wire.write(0x00);
  Wire.endTransmission();
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x1C); Wire.write(0x00);
  Wire.endTransmission();
  Serial.println("[Sensors] FSR sets + MPU-6050 initialised");
}

void sensors_read_all() {
  read_set_a();
  read_set_b();
  park_sets_high_z();
  read_imu();
}

void sensors_calibrate_fsr() {
  for (int i = 0; i < N_FSR; i++) gFsrZero[i] = 0;
  long acc[N_FSR] = {0,0,0,0,0,0};
  for (int s = 0; s < 32; s++) {
    sensors_read_all();
    for (int i = 0; i < N_FSR; i++) acc[i] += gFsr[i];
    delay(1);
  }
  for (int i = 0; i < N_FSR; i++) gFsrZero[i] = (int)(acc[i] / 32);
  Serial.printf("[Cal] FSR zeros: %d %d %d %d %d %d\n",
    gFsrZero[0],gFsrZero[1],gFsrZero[2],gFsrZero[3],gFsrZero[4],gFsrZero[5]);
}

void sensors_calibrate_imu() {
  for (int i = 0; i < 6; i++) gImuOffset[i] = 0;
  double acc[6] = {0,0,0,0,0,0};
  const int N = 64;
  for (int s = 0; s < N; s++) {
    sensors_read_all();
    acc[0] += gAccel[0]; acc[1] += gAccel[1]; acc[2] += gAccel[2];
    acc[3] += gGyro[0];  acc[4] += gGyro[1];  acc[5] += gGyro[2];
    delay(2);
  }
  gImuOffset[0] = (float)(acc[0] / N);
  gImuOffset[1] = (float)(acc[1] / N);
  gImuOffset[2] = (float)(acc[2] / N) - G_TO_MS2;
  gImuOffset[3] = (float)(acc[3] / N);
  gImuOffset[4] = (float)(acc[4] / N);
  gImuOffset[5] = (float)(acc[5] / N);
  Serial.printf("[Cal] IMU offsets accel %.2f %.2f %.2f gyro %.2f %.2f %.2f\n",
    gImuOffset[0],gImuOffset[1],gImuOffset[2],
    gImuOffset[3],gImuOffset[4],gImuOffset[5]);
}
