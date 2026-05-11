// MPU-6050 smoke test for SoleSense v0.2 hardware.
// Flash in place of SoleSenseV2.ino, then open serial at 115200.

#include <Arduino.h>
#include <Wire.h>

static constexpr uint8_t PIN_SDA = 6;   // D4
static constexpr uint8_t PIN_SCL = 7;   // D5
static constexpr uint8_t MPU_ADDR = 0x68;
static constexpr uint8_t REG_PWR_MGMT_1 = 0x6B;
static constexpr uint8_t REG_WHO_AM_I   = 0x75;
static constexpr uint8_t REG_ACCEL_XOUT_H = 0x3B;
static constexpr float ACCEL_LSB_PER_G  = 16384.0f;
static constexpr float GYRO_LSB_PER_DPS = 131.0f;

static bool writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static bool readRegs(uint8_t reg, uint8_t* buf, uint8_t n) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)MPU_ADDR, (int)n, (int)true) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 2000) {}
  delay(200);

  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(400000);

  Serial.println();
  Serial.println("== MPU-6050 smoke test ==");
  Serial.printf("I2C: SDA=GPIO%u  SCL=GPIO%u  addr=0x%02X\n",
                PIN_SDA, PIN_SCL, MPU_ADDR);

  // Bus probe
  Wire.beginTransmission(MPU_ADDR);
  uint8_t ack = Wire.endTransmission();
  if (ack != 0) {
    Serial.printf("FAIL: no ACK at 0x%02X (endTransmission=%u). Check wiring/pull-ups.\n",
                  MPU_ADDR, ack);
    return;
  }
  Serial.println("OK:   slave ACKed.");

  // WHO_AM_I should read 0x68
  uint8_t who = 0xFF;
  if (!readRegs(REG_WHO_AM_I, &who, 1)) {
    Serial.println("FAIL: WHO_AM_I read error.");
    return;
  }
  Serial.printf("WHO_AM_I = 0x%02X  (expect 0x68)\n", who);
  if (who != 0x68) {
    Serial.println("WARN: unexpected ID -- continuing anyway.");
  }

  // Wake from sleep (default state after power-on)
  if (!writeReg(REG_PWR_MGMT_1, 0x00)) {
    Serial.println("FAIL: could not clear PWR_MGMT_1.");
    return;
  }
  delay(50);
  Serial.println("OK:   sensor awake. Streaming...");
  Serial.println("ax(g)\tay(g)\taz(g)\tgx(dps)\tgy(dps)\tgz(dps)\tT(C)");
}

void loop() {
  uint8_t raw[14];
  if (!readRegs(REG_ACCEL_XOUT_H, raw, 14)) {
    Serial.println("read error");
    delay(200);
    return;
  }
  int16_t ax = (raw[0]  << 8) | raw[1];
  int16_t ay = (raw[2]  << 8) | raw[3];
  int16_t az = (raw[4]  << 8) | raw[5];
  int16_t tr = (raw[6]  << 8) | raw[7];
  int16_t gx = (raw[8]  << 8) | raw[9];
  int16_t gy = (raw[10] << 8) | raw[11];
  int16_t gz = (raw[12] << 8) | raw[13];

  float tempC = tr / 340.0f + 36.53f;
  Serial.printf("%+.3f\t%+.3f\t%+.3f\t%+.2f\t%+.2f\t%+.2f\t%.1f\n",
                ax / ACCEL_LSB_PER_G,
                ay / ACCEL_LSB_PER_G,
                az / ACCEL_LSB_PER_G,
                gx / GYRO_LSB_PER_DPS,
                gy / GYRO_LSB_PER_DPS,
                gz / GYRO_LSB_PER_DPS,
                tempC);
  delay(50);   // ~20 Hz
}
