// =============================================================================
// SoleSense v0.2 -- sensors.cpp
// =============================================================================

#include "sensors.h"

#include <Wire.h>

int16_t gFsr[N_FSR]      = {0,0,0,0,0,0};
int16_t gFsrEma[N_FSR]   = {0,0,0,0,0,0};
float   gAccel[3]        = {0,0,0};
float   gGyro[3]         = {0,0,0};
int     gFsrZero[N_FSR]  = {0,0,0,0,0,0};
float   gImuOffset[6]    = {0,0,0,0,0,0};

// 4-sample oversampling kills high-frequency noise (60 Hz hum, switching
// transients, breadboard capacitive coupling). The first read after a
// multiplexer switch is often slightly dirty; averaging 4 reads dilutes
// the warm-up sample without adding a separate discard step. ~12 us per
// channel x 3 channels = ~36 us extra per set, well under the 1.4 ms
// per-sample slack we measured.
static inline int read_adc_oversampled(uint8_t pin) {
  int sum = 0;
  for (uint8_t i = 0; i < 4; i++) sum += analogRead(pin);
  return sum >> 2;
}

// Drive the inactive set's power pin LOW (not floating) so any ghost
// current path from a pressed FSR shorts to GND instead of propagating
// across to the other ADC pins. 200 us settling per set fits the 2 ms
// per-sample budget at 500 Hz with margin and gives the analog domain
// extra headroom on noisy breadboard prototypes.
static void read_set_a() {
  pinMode(PIN_PWR_SET2, OUTPUT);
  digitalWrite(PIN_PWR_SET2, LOW);
  pinMode(PIN_PWR_SET1, OUTPUT);
  digitalWrite(PIN_PWR_SET1, HIGH);
  delayMicroseconds(200);
  gFsr[0] = read_adc_oversampled(PIN_ADC_A) - gFsrZero[0];   // Heel medial
  gFsr[1] = read_adc_oversampled(PIN_ADC_B) - gFsrZero[1];   // Heel lateral
  gFsr[2] = read_adc_oversampled(PIN_ADC_C) - gFsrZero[2];   // Midfoot medial
}

static void read_set_b() {
  pinMode(PIN_PWR_SET1, OUTPUT);
  digitalWrite(PIN_PWR_SET1, LOW);
  pinMode(PIN_PWR_SET2, OUTPUT);
  digitalWrite(PIN_PWR_SET2, HIGH);
  delayMicroseconds(200);
  gFsr[3] = read_adc_oversampled(PIN_ADC_A) - gFsrZero[3];   // Midfoot lateral
  gFsr[4] = read_adc_oversampled(PIN_ADC_B) - gFsrZero[4];   // Forefoot medial
  gFsr[5] = read_adc_oversampled(PIN_ADC_C) - gFsrZero[5];   // Forefoot lateral
}

// Drive both sets LOW between active reads so any leakage path is grounded
// rather than floating. Despite the name's history, this is active-LOW, not
// true high-Z; the LOW state is what kills ghost crosstalk through the
// unpowered FSRs.
static void park_sets_grounded() {
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

  // MPU-6050 wake + default ranges (+/-2g, +/-250 deg/s).
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
  park_sets_grounded();
  read_imu();

  // EMA on the FSR readings for the live UI. gFsr stays raw so the step
  // detector / Kalman / FFT / stats all see unfiltered samples; gFsrEma is
  // strictly for the visual fill of the foot-diagram circles, which were
  // noticeably jittery on the raw signal.
  for (uint8_t i = 0; i < N_FSR; i++) {
    float prev = (float)gFsrEma[i];
    float next = FSR_EMA_ALPHA * (float)gFsr[i] + (1.0f - FSR_EMA_ALPHA) * prev;
    gFsrEma[i] = (int16_t)next;
  }
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
