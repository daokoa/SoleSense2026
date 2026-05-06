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

// FSR matrix: 6 sensors in 2 sets of 3 (A, B, C). No multiplexer.
// Each FSR's pin 2 sits in a voltage divider:
//     <PWR_SETx pin> -- FSR -- <ADC_x pin> -- 10kΩ -- GND
// Two FSRs share each analog input (one from each set). Cross-talk is
// minimised by setting the unpowered set's GPIO to INPUT (high-Z).
#define PIN_ADC_A     2      // GPIO2 / A0 - analog A (FSR 1A and 2A)
#define PIN_ADC_B     3      // GPIO3      - analog B (FSR 1B and 2B)
#define PIN_ADC_C     4      // GPIO4      - analog C (FSR 1C and 2C)
#define PIN_PWR_SET1  5      // GPIO5  - digital power for Set 1 (1A, 1B, 1C)
#define PIN_PWR_SET2  10     // GPIO10 - digital power for Set 2 (2A, 2B, 2C)

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
volatile bool gStartRequested = false;
volatile bool gStopRequested  = false;
volatile bool gSleepRequested = false;
volatile bool gNewSample      = false;

hw_timer_t* gTimer = nullptr;
File        gFile;

static char     gRowBuf[25][128];
static uint8_t  gRowCount    = 0;
static uint32_t gLastFlushMs = 0;

struct Thresholds {
  int hlr        = 100;
  int proneMax   = 15;
  int proneMin   = -8;
  int gct        = 300;
  int cadenceMin = 160;
};
Thresholds gThresholds;

// Forward-declared up here so Arduino IDE's auto-generated prototype
// for readIntParam() can see it.
struct Range { int lo, hi; };
static const Range R_HLR        = {1, 10000};
static const Range R_PRONE_MAX  = {0, 90};
static const Range R_PRONE_MIN  = {-90, 0};
static const Range R_GCT        = {50, 2000};
static const Range R_CAD        = {60, 300};

int     gFsrZero[6]   = {0,0,0,0,0,0};
int16_t gFsr[6]       = {0,0,0,0,0,0};

float gImuOffset[6]   = {0,0,0,0,0,0};
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

// Channel-to-FSR mapping (6 FSRs across 2 sets of 3):
//   gFsr[0] = Set 1 A = Heel
//   gFsr[1] = Set 1 B = Lateral Mid
//   gFsr[2] = Set 1 C = Medial Mid
//   gFsr[3] = Set 2 A = Ball Lateral
//   gFsr[4] = Set 2 B = Ball Medial
//   gFsr[5] = Set 2 C = Toe 1
static void readAllFsr() {
  // Activate Set 1 (Set 2 high-Z)
  pinMode(PIN_PWR_SET2, INPUT);
  pinMode(PIN_PWR_SET1, OUTPUT);
  digitalWrite(PIN_PWR_SET1, HIGH);
  delayMicroseconds(50);                              // FSR + cap settle
  gFsr[0] = analogRead(PIN_ADC_A) - gFsrZero[0];
  gFsr[1] = analogRead(PIN_ADC_B) - gFsrZero[1];
  gFsr[2] = analogRead(PIN_ADC_C) - gFsrZero[2];

  // Activate Set 2 (Set 1 high-Z)
  pinMode(PIN_PWR_SET1, INPUT);
  pinMode(PIN_PWR_SET2, OUTPUT);
  digitalWrite(PIN_PWR_SET2, HIGH);
  delayMicroseconds(50);
  gFsr[3] = analogRead(PIN_ADC_A) - gFsrZero[3];
  gFsr[4] = analogRead(PIN_ADC_B) - gFsrZero[4];
  gFsr[5] = analogRead(PIN_ADC_C) - gFsrZero[5];

  // Park both high-Z between samples
  pinMode(PIN_PWR_SET1, INPUT);
  pinMode(PIN_PWR_SET2, INPUT);
}

static void readImu() {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x3B);
  Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)MPU6050_ADDR, (uint8_t)14);

  int16_t ax = (Wire.read() << 8) | Wire.read();
  int16_t ay = (Wire.read() << 8) | Wire.read();
  int16_t az = (Wire.read() << 8) | Wire.read();
  Wire.read(); Wire.read();
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
  // FSR power lines park at high-Z until first read; analog inputs are inputs
  pinMode(PIN_PWR_SET1, INPUT);
  pinMode(PIN_PWR_SET2, INPUT);
  pinMode(PIN_ADC_A, INPUT);
  pinMode(PIN_ADC_B, INPUT);
  pinMode(PIN_ADC_C, INPUT);
  analogReadResolution(12);

  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(400000);
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
  gImuOffset[2] = (float)(acc[2] / N) - G_TO_MS2;
  gImuOffset[3] = (float)(acc[3] / N);
  gImuOffset[4] = (float)(acc[4] / N);
  gImuOffset[5] = (float)(acc[5] / N);
  saveImuOffsets();

  Serial.printf("[Cal] IMU offsets accel %.2f %.2f %.2f gyro %.2f %.2f %.2f\n",
    gImuOffset[0],gImuOffset[1],gImuOffset[2],
    gImuOffset[3],gImuOffset[4],gImuOffset[5]);
}

// =============================================================================
// 50 Hz sample timer
// =============================================================================

void IRAM_ATTR onSampleTick() {
  gNewSample = true;
}

static void startSampleTimer() {
  gTimer = timerBegin(1000000);                 // 1 MHz tick
  if (!gTimer) { Serial.println("[Timer] alloc FAILED"); return; }
  timerAttachInterrupt(gTimer, &onSampleTick);
  timerAlarm(gTimer, 20000, true, 0);           // 20 ms = 50 Hz, autoreload
}

static void stopSampleTimer() {
  if (gTimer) {
    timerEnd(gTimer);
    gTimer = nullptr;
  }
  gNewSample = false;
}

// =============================================================================
// Sample loop + CSV writer
// =============================================================================

static void flushRowBuffer() {
  if (gRowCount == 0 || !gFile) return;
  for (uint8_t i = 0; i < gRowCount; i++) {
    gFile.write((const uint8_t*)gRowBuf[i], strlen(gRowBuf[i]));
  }
  gRowCount = 0;
  gLastFlushMs = millis();
}

static void takeSample() {
  readAllFsr();
  readImu();
  uint32_t ts = millis();
  snprintf(gRowBuf[gRowCount], sizeof(gRowBuf[0]),
    "%lu,%d,%d,%d,%d,%d,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f\n",
    (unsigned long)ts,
    gFsr[0],gFsr[1],gFsr[2],gFsr[3],gFsr[4],gFsr[5],
    gAccel[0],gAccel[1],gAccel[2],
    gGyro[0], gGyro[1], gGyro[2]);
  gRowCount++;
  if (gRowCount >= 25 || (millis() - gLastFlushMs) >= 500) {
    flushRowBuffer();
  }
}

// =============================================================================
// Deep sleep
// =============================================================================

static void enterDeepSleep() {
  Serial.println("[Sleep] entering deep sleep, wake on GPIO9 LOW");
  Serial.flush();
  delay(150);                              // let HTTP response flush

  WiFi.softAPdisconnect(true);
  LittleFS.end();

  // Hold the wake pin high during sleep so a press to GND triggers wake reliably
  gpio_pullup_en((gpio_num_t)PIN_WAKE);
  gpio_pulldown_dis((gpio_num_t)PIN_WAKE);

  esp_deep_sleep_enable_gpio_wakeup(1ULL << PIN_WAKE, ESP_GPIO_WAKEUP_GPIO_LOW);
  esp_deep_sleep_start();                  // never returns
}

// =============================================================================
// State machine
// =============================================================================

static void enterRecording() {
  gFile = LittleFS.open("/data.csv", "w");
  if (!gFile) {
    Serial.println("[REC] open /data.csv FAILED");
    return;
  }
  gRowCount    = 0;
  gLastFlushMs = millis();
  startSampleTimer();
  gState = RECORDING;
  Serial.println("[REC] started");
}

static void exitRecording() {
  stopSampleTimer();
  flushRowBuffer();
  if (gFile) { gFile.flush(); gFile.close(); }
  gState = IDLE;
  Serial.println("[REC] stopped");
}

static void processRequests() {
  if (gStartRequested) {
    gStartRequested = false;
    if (gState == IDLE) enterRecording();
  }
  if (gStopRequested) {
    gStopRequested = false;
    if (gState == RECORDING) exitRecording();
  }
  if (gSleepRequested) {
    gSleepRequested = false;
    enterDeepSleep();                      // never returns
  }
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

static bool readIntParam(AsyncWebServerRequest* req, const char* key, Range r,
                         int& out, bool& touched, String& err) {
  if (!req->hasParam(key, true)) return true;
  int v = req->getParam(key, true)->value().toInt();
  if (v < r.lo || v > r.hi) {
    err = String(key) + " out of range";
    return false;
  }
  out = v;
  touched = true;
  return true;
}

static void handleSettings(AsyncWebServerRequest* req) {
  Thresholds next = gThresholds;
  bool touched = false;
  String err;
  if (!readIntParam(req, "hlr",        R_HLR,       next.hlr,        touched, err) ||
      !readIntParam(req, "proneMax",   R_PRONE_MAX, next.proneMax,   touched, err) ||
      !readIntParam(req, "proneMin",   R_PRONE_MIN, next.proneMin,   touched, err) ||
      !readIntParam(req, "gct",        R_GCT,       next.gct,        touched, err) ||
      !readIntParam(req, "cadenceMin", R_CAD,       next.cadenceMin, touched, err)) {
    req->send(400, "application/json",
              "{\"ok\":false,\"error\":\"" + err + "\"}");
    return;
  }
  if (touched) {
    gThresholds = next;
    saveThresholds();
  }
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

static void handleStart(AsyncWebServerRequest* req) {
  if (gState != IDLE) {
    req->send(409, "application/json", "{\"ok\":false,\"error\":\"already recording\"}");
    return;
  }
  gStartRequested = true;
  req->send(200, "application/json", "{\"ok\":true}");
}

static void handleStop(AsyncWebServerRequest* req) {
  if (gState != RECORDING) {
    req->send(409, "application/json", "{\"ok\":false,\"error\":\"not recording\"}");
    return;
  }
  gStopRequested = true;
  req->send(200, "application/json", "{\"ok\":true}");
}

static void handleDataCsv(AsyncWebServerRequest* req) {
  if (gState == RECORDING) {
    req->send(409, "text/plain", "recording in progress");
    return;
  }
  if (!LittleFS.exists("/data.csv")) {
    req->send(404, "text/plain", "no data");
    return;
  }
  req->send(LittleFS, "/data.csv", "text/csv");
}

static void handleDataClear(AsyncWebServerRequest* req) {
  if (gState == RECORDING) {
    req->send(409, "application/json", "{\"ok\":false,\"error\":\"recording\"}");
    return;
  }
  if (LittleFS.exists("/data.csv")) LittleFS.remove("/data.csv");
  req->send(200, "application/json", "{\"ok\":true}");
}

static void handleSleep(AsyncWebServerRequest* req) {
  if (gState == RECORDING) {
    req->send(409, "application/json", "{\"ok\":false,\"error\":\"recording\"}");
    return;
  }
  gSleepRequested = true;
  req->send(200, "application/json", "{\"ok\":true}");
}

// Live sensor snapshot for the frontend's 5 Hz polling UI.
// When IDLE the sample loop is quiet, so we read fresh values here.
// When RECORDING the 50 Hz timer keeps gFsr/gAccel/gGyro current; we just read them.
static void handleSensor(AsyncWebServerRequest* req) {
  if (gState == IDLE) {
    readAllFsr();
    readImu();
  }
  char body[256];
  snprintf(body, sizeof(body),
    "{\"ax\":%.3f,\"ay\":%.3f,\"az\":%.3f,"
    "\"gx\":%.3f,\"gy\":%.3f,\"gz\":%.3f,"
    "\"fsr\":[%d,%d,%d,%d,%d,%d]}",
    gAccel[0], gAccel[1], gAccel[2],
    gGyro[0],  gGyro[1],  gGyro[2],
    gFsr[0], gFsr[1], gFsr[2], gFsr[3], gFsr[4], gFsr[5]);
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
  pinMode(PIN_WAKE, INPUT_PULLUP);
  loadSettings();

  WiFi.softAP(AP_SSID, AP_PASS);
  IPAddress ip = WiFi.softAPIP();
  Serial.printf("[WiFi] AP '%s' up at %s\n", AP_SSID, ip.toString().c_str());

  server.on("/api/device",          HTTP_GET,  handleDevice);
  server.on("/api/settings",        HTTP_POST, handleSettings);
  server.on("/api/calibrate/zero",  HTTP_POST, handleCalibrateZero);
  server.on("/api/calibrate/imu",   HTTP_POST, handleCalibrateImu);
  server.on("/api/start",           HTTP_POST, handleStart);
  server.on("/api/stop",            HTTP_POST, handleStop);
  server.on("/data.csv",            HTTP_GET,  handleDataCsv);
  server.on("/api/data/clear",      HTTP_POST, handleDataClear);
  server.on("/api/sleep",           HTTP_POST, handleSleep);
  server.on("/api/sensor",          HTTP_GET,  handleSensor);
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  server.begin();
  Serial.println("[HTTP] server started");
}

void loop() {
  processRequests();
  if (gState == RECORDING && gNewSample) {
    gNewSample = false;
    takeSample();
  }
  delay(1);
}
