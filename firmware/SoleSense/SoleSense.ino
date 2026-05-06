// =============================================================================
// SoleSense v0.1 — XIAO ESP32-C3 firmware
// =============================================================================

#include <WiFi.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <Wire.h>

// =============================================================================
// Pins
// =============================================================================

#define PIN_SDA       6      // I2C - MPU-6050
#define PIN_SCL       7      // I2C - MPU-6050
#define PIN_MUX_SIG   A0     // GPIO2 - analog mux output
#define PIN_MUX_S0    D0     // GPIO3
#define PIN_MUX_S1    D1     // GPIO4
#define PIN_MUX_S2    D2     // GPIO5
#define PIN_WAKE      9      // GPIO9 - on-board BOOT button doubles as wake button

#define MPU6050_ADDR  0x68
#define ACCEL_LSB_PER_G   16384.0f
#define G_TO_MS2          9.80665f
#define GYRO_LSB_PER_DPS  131.0f

static const char* AP_SSID = "SoleSense";
static const char* AP_PASS = "solesense";

AsyncWebServer server(80);

// =============================================================================
// HTTP routes
// =============================================================================

enum State { IDLE, RECORDING };
volatile State gState = IDLE;

struct Thresholds {
  int hlr        = 100;
  int proneMax   = 15;
  int proneMin   = -8;
  int gct        = 300;
  int cadenceMin = 160;
};
Thresholds gThresholds;

// FSR state
int     gFsrZero[6]   = {0,0,0,0,0,0};
int16_t gFsr[6]       = {0,0,0,0,0,0};

// IMU state — offsets in physical units (m/s2 and deg/s)
float gImuOffset[6]   = {0,0,0,0,0,0};   // ax, ay, az, gx, gy, gz
float gAccel[3]       = {0,0,0};
float gGyro[3]        = {0,0,0};

// =============================================================================
// NVS settings (Preferences)
// =============================================================================

Preferences gPrefs;

static void loadSettings() {
  gPrefs.begin("solesense", false);
  gThresholds.hlr        = gPrefs.getInt("hlr",        100);
  gThresholds.proneMax   = gPrefs.getInt("proneMax",   15);
  gThresholds.proneMin   = gPrefs.getInt("proneMin",   -8);
  gThresholds.gct        = gPrefs.getInt("gct",        300);
  gThresholds.cadenceMin = gPrefs.getInt("cadenceMin", 160);

  static const char* fsrKeys[6] = {"fsrZ0","fsrZ1","fsrZ2","fsrZ3","fsrZ4","fsrZ5"};
  for (int i = 0; i < 6; i++) gFsrZero[i] = gPrefs.getInt(fsrKeys[i], 0);

  gImuOffset[0] = gPrefs.getFloat("imuOax", 0);
  gImuOffset[1] = gPrefs.getFloat("imuOay", 0);
  gImuOffset[2] = gPrefs.getFloat("imuOaz", 0);
  gImuOffset[3] = gPrefs.getFloat("imuOgx", 0);
  gImuOffset[4] = gPrefs.getFloat("imuOgy", 0);
  gImuOffset[5] = gPrefs.getFloat("imuOgz", 0);

  Serial.println("[NVS] thresholds + calibration loaded");
}

static void saveThresholds() {
  gPrefs.putInt("hlr",        gThresholds.hlr);
  gPrefs.putInt("proneMax",   gThresholds.proneMax);
  gPrefs.putInt("proneMin",   gThresholds.proneMin);
  gPrefs.putInt("gct",        gThresholds.gct);
  gPrefs.putInt("cadenceMin", gThresholds.cadenceMin);
}

// =============================================================================
// Sensor reads
// =============================================================================

static int readFsr(uint8_t channel) {
  digitalWrite(PIN_MUX_S0, channel & 0x01);
  digitalWrite(PIN_MUX_S1, (channel >> 1) & 0x01);
  digitalWrite(PIN_MUX_S2, (channel >> 2) & 0x01);
  delayMicroseconds(10);
  return analogRead(PIN_MUX_SIG) - gFsrZero[channel];
}

static void readAllFsr() {
  for (uint8_t ch = 0; ch < 6; ch++) gFsr[ch] = readFsr(ch);
}

static void readImu() {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x3B);                    // ACCEL_XOUT_H
  Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)MPU6050_ADDR, (uint8_t)14);

  int16_t ax = (Wire.read() << 8) | Wire.read();
  int16_t ay = (Wire.read() << 8) | Wire.read();
  int16_t az = (Wire.read() << 8) | Wire.read();
  Wire.read(); Wire.read();            // discard temp
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

static void initSensors() {
  // mux selects + sig input
  pinMode(PIN_MUX_S0, OUTPUT);
  pinMode(PIN_MUX_S1, OUTPUT);
  pinMode(PIN_MUX_S2, OUTPUT);
  pinMode(PIN_MUX_SIG, INPUT);
  analogReadResolution(12);

  // I2C + MPU-6050 wake
  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(400000);
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x6B); Wire.write(0x00);    // PWR_MGMT_1 = 0 (wake, default clock)
  Wire.endTransmission();
  delay(10);
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x1B); Wire.write(0x00);    // GYRO_CONFIG = 0 -> +/-250 dps
  Wire.endTransmission();
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x1C); Wire.write(0x00);    // ACCEL_CONFIG = 0 -> +/-2g
  Wire.endTransmission();
  Serial.println("[Sensors] mux + MPU-6050 initialised");
}

// =============================================================================
// Calibration
// =============================================================================

static void saveFsrZeros() {
  static const char* keys[6] = {"fsrZ0","fsrZ1","fsrZ2","fsrZ3","fsrZ4","fsrZ5"};
  for (int i = 0; i < 6; i++) gPrefs.putInt(keys[i], gFsrZero[i]);
}

static void saveImuOffsets() {
  gPrefs.putFloat("imuOax", gImuOffset[0]);
  gPrefs.putFloat("imuOay", gImuOffset[1]);
  gPrefs.putFloat("imuOaz", gImuOffset[2]);
  gPrefs.putFloat("imuOgx", gImuOffset[3]);
  gPrefs.putFloat("imuOgy", gImuOffset[4]);
  gPrefs.putFloat("imuOgz", gImuOffset[5]);
}

static void calibrateFsrZero() {
  // Zero the offsets so readFsr returns raw ADC during calibration
  for (int i = 0; i < 6; i++) gFsrZero[i] = 0;

  long acc[6] = {0,0,0,0,0,0};
  for (int s = 0; s < 32; s++) {
    for (uint8_t ch = 0; ch < 6; ch++) acc[ch] += readFsr(ch);
    delay(1);
  }
  for (int i = 0; i < 6; i++) gFsrZero[i] = (int)(acc[i] / 32);
  saveFsrZeros();

  Serial.printf("[Cal] FSR zeros: %d %d %d %d %d %d\n",
    gFsrZero[0],gFsrZero[1],gFsrZero[2],gFsrZero[3],gFsrZero[4],gFsrZero[5]);
}

static void calibrateImu() {
  // Zero the offsets so readImu returns offset-free physical values during calibration
  for (int i = 0; i < 6; i++) gImuOffset[i] = 0;

  double acc[6] = {0,0,0,0,0,0};
  const int N = 64;
  for (int s = 0; s < N; s++) {
    readImu();
    acc[0] += gAccel[0]; acc[1] += gAccel[1]; acc[2] += gAccel[2];
    acc[3] += gGyro[0];  acc[4] += gGyro[1];  acc[5] += gGyro[2];
    delay(2);
  }
  gImuOffset[0] = (float)(acc[0] / N);
  gImuOffset[1] = (float)(acc[1] / N);
  gImuOffset[2] = (float)(acc[2] / N) - G_TO_MS2;     // gravity stays on Z
  gImuOffset[3] = (float)(acc[3] / N);
  gImuOffset[4] = (float)(acc[4] / N);
  gImuOffset[5] = (float)(acc[5] / N);
  saveImuOffsets();

  Serial.printf("[Cal] IMU offsets accel %.2f %.2f %.2f gyro %.2f %.2f %.2f\n",
    gImuOffset[0],gImuOffset[1],gImuOffset[2],
    gImuOffset[3],gImuOffset[4],gImuOffset[5]);
}

static String stateName(State s) { return s == IDLE ? "idle" : "recording"; }

static void handleDevice(AsyncWebServerRequest* req) {
  String json = "{";
  json += "\"firmware\":\"SoleSense v0.1\",";
  json += "\"board\":\"XIAO ESP32-C3\",";
  json += "\"sampleRateHz\":50,";
  json += "\"state\":\"" + stateName(gState) + "\",";
  json += "\"fs\":{\"totalBytes\":" + String((unsigned)LittleFS.totalBytes());
  json +=        ",\"usedBytes\":"  + String((unsigned)LittleFS.usedBytes()) + "},";
  json += "\"hasData\":" + String(LittleFS.exists("/data.csv") ? "true" : "false") + ",";
  json += "\"thresholds\":{";
  json +=   "\"hlr\":"        + String(gThresholds.hlr) + ",";
  json +=   "\"proneMax\":"   + String(gThresholds.proneMax) + ",";
  json +=   "\"proneMin\":"   + String(gThresholds.proneMin) + ",";
  json +=   "\"gct\":"        + String(gThresholds.gct) + ",";
  json +=   "\"cadenceMin\":" + String(gThresholds.cadenceMin);
  json += "}}";
  req->send(200, "application/json", json);
}

struct Range { int lo, hi; };
static const Range R_HLR        = {1, 10000};
static const Range R_PRONE_MAX  = {0, 90};
static const Range R_PRONE_MIN  = {-90, 0};
static const Range R_GCT        = {50, 2000};
static const Range R_CAD        = {60, 300};

static bool readIntParam(AsyncWebServerRequest* req, const char* key, Range r,
                         int& out, String& err) {
  if (!req->hasParam(key, true)) return true;       // optional, leave unchanged
  int v = req->getParam(key, true)->value().toInt();
  if (v < r.lo || v > r.hi) {
    err = String(key) + " out of range";
    return false;
  }
  out = v;
  return true;
}

static void handleSettings(AsyncWebServerRequest* req) {
  Thresholds next = gThresholds;
  String err;
  if (!readIntParam(req, "hlr",        R_HLR,       next.hlr,        err) ||
      !readIntParam(req, "proneMax",   R_PRONE_MAX, next.proneMax,   err) ||
      !readIntParam(req, "proneMin",   R_PRONE_MIN, next.proneMin,   err) ||
      !readIntParam(req, "gct",        R_GCT,       next.gct,        err) ||
      !readIntParam(req, "cadenceMin", R_CAD,       next.cadenceMin, err)) {
    req->send(400, "application/json",
              "{\"ok\":false,\"error\":\"" + err + "\"}");
    return;
  }
  gThresholds = next;
  saveThresholds();
  req->send(200, "application/json", "{\"ok\":true}");
}

static void handleCalibrateZero(AsyncWebServerRequest* req) {
  if (gState != IDLE) {
    req->send(409, "application/json", "{\"ok\":false,\"error\":\"recording\"}");
    return;
  }
  calibrateFsrZero();
  String json = "{\"ok\":true,\"fsrZero\":[";
  for (int i = 0; i < 6; i++) {
    json += String(gFsrZero[i]);
    if (i < 5) json += ",";
  }
  json += "]}";
  req->send(200, "application/json", json);
}

static void handleCalibrateImu(AsyncWebServerRequest* req) {
  if (gState != IDLE) {
    req->send(409, "application/json", "{\"ok\":false,\"error\":\"recording\"}");
    return;
  }
  calibrateImu();
  char body[200];
  snprintf(body, sizeof(body),
    "{\"ok\":true,\"accel\":[%.4f,%.4f,%.4f],\"gyro\":[%.4f,%.4f,%.4f]}",
    gImuOffset[0],gImuOffset[1],gImuOffset[2],
    gImuOffset[3],gImuOffset[4],gImuOffset[5]);
  req->send(200, "application/json", body);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("=== SoleSense booting ===");

  if (!LittleFS.begin()) {
    Serial.println("[FS] mount FAILED");
  } else {
    Serial.printf("[FS] Mounted - %u / %u bytes used\n",
                  (unsigned)LittleFS.usedBytes(), (unsigned)LittleFS.totalBytes());
  }

  initSensors();
  loadSettings();

  WiFi.softAP(AP_SSID, AP_PASS);
  IPAddress ip = WiFi.softAPIP();
  Serial.printf("[WiFi] AP '%s' up at %s\n", AP_SSID, ip.toString().c_str());

  server.on("/api/device",          HTTP_GET,  handleDevice);
  server.on("/api/settings",        HTTP_POST, handleSettings);
  server.on("/api/calibrate/zero",  HTTP_POST, handleCalibrateZero);
  server.on("/api/calibrate/imu",   HTTP_POST, handleCalibrateImu);
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  server.begin();
  Serial.println("[HTTP] server started");
}

void loop() {
  delay(1);
}
