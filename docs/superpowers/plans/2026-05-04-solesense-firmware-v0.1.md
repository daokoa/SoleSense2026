# SoleSense Firmware v0.1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the v0.1 SoleSense firmware skeleton -- WiFi AP, LittleFS, all 10 HTTP endpoints, 50 Hz buffered sampling of 6 FSRs + MPU-6050, NVS-backed thresholds + calibration, and GPIO9 deep-sleep -- flashed to the XIAO ESP32-C3, smoke-tested end-to-end with the frontend.

**Architecture:** Single-file Arduino sketch (`firmware/SoleSense/SoleSense.ino`). Hardware timer ISR sets a flag; `loop()` drains it on the main task. HTTP handlers (running on the AsyncWebServer task) only set request flags -- they never touch the file, timer, or sleep API directly. All file/timer/sleep work happens on a single task to avoid mutex bugs.

**Tech Stack:**
- Seeed XIAO ESP32-C3 (ESP32 Arduino core 3.x)
- `ESPAsyncWebServer` + `AsyncTCP` (ESP32Async)
- `LittleFS` (built-in)
- `Preferences` (NVS, built-in)
- Direct register-level MPU-6050 reads via `Wire.h` (no MPU6050 library needed for v0.1)

**Spec reference:** `docs/superpowers/specs/2026-05-04-solesense-firmware-design.md`

**Workflow per task:**
1. Edit code in Arduino IDE (file `firmware/SoleSense/SoleSense.ino`)
2. Compile + Upload (Arduino IDE -> `->` button)
3. Open Serial Monitor (115200 baud) and verify expected output
4. Run any `curl` smoke test from a laptop on the `SoleSense` AP
5. `git commit` from the project root

**Board settings reminder** (Arduino IDE -> Tools menu, set once before Task 1):
- Board: `XIAO_ESP32C3`
- USB CDC On Boot: `Enabled`
- Partition Scheme: `Default 4MB with spiffs (1.2MB APP / 1.5MB SPIFFS)`
- Flash Size: `4MB (32Mb)`
- CPU Frequency: `160MHz`

---

## File Structure

```
firmware/
|-- pseudocode                  # already in repo, untouched
`-- SoleSense/                  # new -- Arduino sketch root
    |-- SoleSense.ino           # all firmware code (~700 lines)
    `-- data/
        `-- index.html          # frontend SPA (placeholder until real SPA dropped in)
```

`SoleSense.ino` is sectioned with banner comments in this order: includes/pins/globals -> NVS -> sensors -> calibration -> sample loop/ring buffer -> timer ISR -> HTTP routes -> deep sleep -> `setup()` + `loop()`.

---

## Task 1: Project skeleton -- empty sketch that compiles

**Files:**
- Create: `firmware/SoleSense/SoleSense.ino`
- Create: `firmware/SoleSense/data/.gitkeep`

- [ ] **Step 1: Create the empty sketch**

`firmware/SoleSense/SoleSense.ino`:
```cpp
// =============================================================================
// SoleSense v0.1 -- XIAO ESP32-C3 firmware
// =============================================================================

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("=== SoleSense booting ===");
}

void loop() {
  delay(1000);
}
```

- [ ] **Step 2: Create the data directory placeholder**

`firmware/SoleSense/data/.gitkeep`: empty file.

- [ ] **Step 3: Compile in Arduino IDE**

Open `firmware/SoleSense/SoleSense.ino` in Arduino IDE, hit the checkmark (`[x]` Verify) button.

Expected: compiles without errors. If "ESPAsyncWebServer not found" or similar -- install per SOLESENSE.md 12.3 (won't actually be needed until Task 2, but verify libraries are present now).

- [ ] **Step 4: Flash and verify boot output**

Click `->` (Upload). Open Serial Monitor at 115200 baud. Tap reset on board if needed.

Expected output:
```
=== SoleSense booting ===
```

- [ ] **Step 5: Commit**

```bash
cd "/Users/daodoan/Documents/Obsidian Vault/obsidian/solesense"
git add firmware/SoleSense/
git commit -m "feat(firmware): scaffold SoleSense v0.1 sketch"
```

---

## Task 2: Boot, WiFi AP visible from phone

**Files:**
- Modify: `firmware/SoleSense/SoleSense.ino`

- [ ] **Step 1: Add WiFi AP to setup()**

Replace the contents of `firmware/SoleSense/SoleSense.ino`:
```cpp
// =============================================================================
// SoleSense v0.1 -- XIAO ESP32-C3 firmware
// =============================================================================

#include <WiFi.h>

static const char* AP_SSID = "SoleSense";
static const char* AP_PASS = "solesense";

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("=== SoleSense booting ===");

  WiFi.softAP(AP_SSID, AP_PASS);
  IPAddress ip = WiFi.softAPIP();
  Serial.printf("[WiFi] AP '%s' up at %s\n", AP_SSID, ip.toString().c_str());
}

void loop() {
  delay(1000);
}
```

- [ ] **Step 2: Compile + flash**

Upload via Arduino IDE.

- [ ] **Step 3: Verify Serial output**

Expected:
```
=== SoleSense booting ===
[WiFi] AP 'SoleSense' up at 192.168.4.1
```

- [ ] **Step 4: Verify AP visibility from phone**

On your phone, open WiFi settings. Confirm `SoleSense` is in the list. Connect with password `solesense`. Phone should associate (no internet, expected).

- [ ] **Step 5: Commit**

```bash
git add firmware/SoleSense/SoleSense.ino
git commit -m "feat(firmware): start WiFi AP on boot"
```

---

## Task 3: LittleFS mount + serve placeholder index.html at GET /

**Files:**
- Modify: `firmware/SoleSense/SoleSense.ino`
- Create: `firmware/SoleSense/data/index.html`

- [ ] **Step 1: Create a minimal placeholder index.html**

`firmware/SoleSense/data/index.html` -- a one-page test panel that hits every endpoint via `fetch`. This is your fallback demo UI; swap in the real SPA in Task 13.

```html
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SoleSense Test Panel</title>
<style>
  body { font-family: -apple-system, sans-serif; max-width: 540px; margin: 24px auto; padding: 0 16px; color: #111; }
  h1 { font-size: 22px; margin: 0 0 4px; }
  .sub { color: #666; margin-bottom: 24px; font-size: 14px; }
  button { font-size: 15px; padding: 10px 14px; margin: 4px 6px 4px 0; border: 1px solid #2563eb; background: #2563eb; color: white; border-radius: 8px; cursor: pointer; }
  button.ghost { background: white; color: #2563eb; }
  button.danger { background: #ef4444; border-color: #ef4444; }
  pre { background: #f3f4f6; padding: 12px; border-radius: 8px; font-size: 12px; overflow: auto; white-space: pre-wrap; }
  section { margin: 18px 0; }
  h2 { font-size: 14px; text-transform: uppercase; color: #6b7280; letter-spacing: 0.04em; margin-bottom: 6px; }
</style>
</head>
<body>
  <h1>SoleSense Test Panel</h1>
  <div class="sub">v0.1 placeholder -- drop in real SPA later</div>

  <section>
    <h2>Recording</h2>
    <button onclick="api('POST','/api/start')">Start Run</button>
    <button class="danger" onclick="api('POST','/api/stop')">Stop Run</button>
  </section>

  <section>
    <h2>Calibrate (insole off the foot)</h2>
    <button class="ghost" onclick="api('POST','/api/calibrate/zero')">FSR zero</button>
    <button class="ghost" onclick="api('POST','/api/calibrate/imu')">IMU zero</button>
  </section>

  <section>
    <h2>Data</h2>
    <button class="ghost" onclick="window.location='/data.csv'">Download CSV</button>
    <button class="danger" onclick="api('POST','/api/data/clear')">Clear data</button>
  </section>

  <section>
    <h2>Device</h2>
    <button class="ghost" onclick="api('GET','/api/device')">/api/device</button>
    <button class="danger" onclick="if(confirm('Sleep now?'))api('POST','/api/sleep')">Sleep</button>
  </section>

  <pre id="out">Waiting for action...</pre>

<script>
const out = document.getElementById('out');
async function api(method, path, body) {
  out.textContent = method + ' ' + path + ' ...';
  try {
    const res = await fetch(path, { method, body });
    const text = await res.text();
    out.textContent = res.status + ' ' + path + '\n\n' + text;
  } catch (e) {
    out.textContent = 'ERR ' + e.message;
  }
}
api('GET','/api/device');
</script>
</body>
</html>
```

- [ ] **Step 2: Add LittleFS mount + static serving to firmware**

Replace `firmware/SoleSense/SoleSense.ino`:
```cpp
// =============================================================================
// SoleSense v0.1 -- XIAO ESP32-C3 firmware
// =============================================================================

#include <WiFi.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>

static const char* AP_SSID = "SoleSense";
static const char* AP_PASS = "solesense";

AsyncWebServer server(80);

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

  WiFi.softAP(AP_SSID, AP_PASS);
  IPAddress ip = WiFi.softAPIP();
  Serial.printf("[WiFi] AP '%s' up at %s\n", AP_SSID, ip.toString().c_str());

  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  server.begin();
  Serial.println("[HTTP] server started");
}

void loop() {
  delay(1);
}
```

- [ ] **Step 3: Compile, flash sketch**

Upload via Arduino IDE.

- [ ] **Step 4: Upload LittleFS data**

Arduino IDE -> `Cmd+Shift+P` -> `Upload LittleFS to Pico/ESP8266/ESP32` -> Enter. Wait for "LittleFS Image Uploaded" in console.

If the plugin command isn't there, install per SOLESENSE.md 12.5.

**Important:** before each LittleFS upload, **close Serial Monitor** (it holds the port).

- [ ] **Step 5: Verify Serial output**

Reset board. Expected:
```
=== SoleSense booting ===
[FS] Mounted - <some bytes> / 1441792 bytes used
[WiFi] AP 'SoleSense' up at 192.168.4.1
[HTTP] server started
```

- [ ] **Step 6: Verify from phone browser**

Phone -> connect to `SoleSense` WiFi -> open `http://192.168.4.1/` in Safari/Chrome. Expected: the test panel loads, "Waiting for action..." text visible, all buttons render.

(Buttons will fail with 404 -- endpoints don't exist yet. That's fine.)

- [ ] **Step 7: Commit**

```bash
git add firmware/SoleSense/
git commit -m "feat(firmware): mount LittleFS and serve test panel at GET /"
```

---

## Task 4: GET /api/device endpoint with hardcoded thresholds

**Files:**
- Modify: `firmware/SoleSense/SoleSense.ino`

- [ ] **Step 1: Add device-info handler**

Add this block to `SoleSense.ino` _before_ `setup()`:
```cpp
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
```

In `setup()`, **before** `server.begin()`, register the route:
```cpp
  server.on("/api/device", HTTP_GET, handleDevice);
```

- [ ] **Step 2: Compile + flash**

Upload sketch (no LittleFS re-upload needed).

- [ ] **Step 3: Smoke test from laptop on AP**

```bash
curl -s http://192.168.4.1/api/device | python3 -m json.tool
```

Expected:
```json
{
    "firmware": "SoleSense v0.1",
    "board": "XIAO ESP32-C3",
    "sampleRateHz": 50,
    "state": "idle",
    "fs": { "totalBytes": 1441792, "usedBytes": <N> },
    "hasData": false,
    "thresholds": {
      "hlr": 100,
      "proneMax": 15,
      "proneMin": -8,
      "gct": 300,
      "cadenceMin": 160
    }
}
```

- [ ] **Step 4: Verify from test panel**

In phone browser: tap `/api/device` button. Pre block fills with the same JSON. **(Screenshot opportunity for slides.)**

- [ ] **Step 5: Commit**

```bash
git add firmware/SoleSense/SoleSense.ino
git commit -m "feat(firmware): add GET /api/device endpoint"
```

---

## Task 5: NVS-backed thresholds + POST /api/settings

**Files:**
- Modify: `firmware/SoleSense/SoleSense.ino`

- [ ] **Step 1: Add Preferences include and helpers**

At the top of `SoleSense.ino`, after `#include <ESPAsyncWebServer.h>`:
```cpp
#include <Preferences.h>
```

Add this section _between_ the `Thresholds` struct and the HTTP route handlers:
```cpp
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
  Serial.printf("[NVS] thresholds loaded: hlr=%d proneMax=%d proneMin=%d gct=%d cadMin=%d\n",
    gThresholds.hlr, gThresholds.proneMax, gThresholds.proneMin,
    gThresholds.gct, gThresholds.cadenceMin);
}

static void saveThresholds() {
  gPrefs.putInt("hlr",        gThresholds.hlr);
  gPrefs.putInt("proneMax",   gThresholds.proneMax);
  gPrefs.putInt("proneMin",   gThresholds.proneMin);
  gPrefs.putInt("gct",        gThresholds.gct);
  gPrefs.putInt("cadenceMin", gThresholds.cadenceMin);
}
```

- [ ] **Step 2: Add /api/settings handler**

In the HTTP routes section, add:
```cpp
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
```

In `setup()`, **after** `LittleFS.begin()` and **before** the WiFi line, add:
```cpp
  loadSettings();
```

In the route registration block (before `server.begin()`):
```cpp
  server.on("/api/settings", HTTP_POST, handleSettings);
```

- [ ] **Step 3: Compile + flash**

- [ ] **Step 4: Smoke test**

```bash
# Update one threshold
curl -s -X POST -d 'cadenceMin=170' http://192.168.4.1/api/settings
# Expected: {"ok":true}

# Verify it persisted via /api/device
curl -s http://192.168.4.1/api/device | python3 -m json.tool | grep cadenceMin
# Expected: "cadenceMin": 170

# Test validation
curl -s -X POST -d 'cadenceMin=999' http://192.168.4.1/api/settings
# Expected: {"ok":false,"error":"cadenceMin out of range"}

# Reset board, verify persistence
# (tap RESET button or unplug/replug USB)
curl -s http://192.168.4.1/api/device | python3 -m json.tool | grep cadenceMin
# Expected: still "cadenceMin": 170
```

- [ ] **Step 5: Commit**

```bash
git add firmware/SoleSense/SoleSense.ino
git commit -m "feat(firmware): persist thresholds to NVS via /api/settings"
```

---

## Task 6: Sensor init (mux, ADC, I^2C, MPU-6050) + read primitives

**Files:**
- Modify: `firmware/SoleSense/SoleSense.ino`

This task wires up sensor *reads* but does not yet sample on a timer. After this task you can poll the sensors from any context.

- [ ] **Step 1: Add Wire include + pin defs + globals**

Add to the top of `SoleSense.ino` (after existing includes):
```cpp
#include <Wire.h>
```

Add this block right after the includes:
```cpp
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
```

Add these globals just below the `Thresholds gThresholds;` line:
```cpp
// FSR state
int     gFsrZero[6]   = {0,0,0,0,0,0};
int16_t gFsr[6]       = {0,0,0,0,0,0};

// IMU state -- offsets in physical units (m/s2 and deg/s)
float gImuOffset[6]   = {0,0,0,0,0,0};   // ax, ay, az, gx, gy, gz
float gAccel[3]       = {0,0,0};
float gGyro[3]        = {0,0,0};
```

- [ ] **Step 2: Add sensor read primitives**

Add this section _between_ NVS settings and HTTP routes:
```cpp
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
```

- [ ] **Step 3: Load NVS calibration values**

Update `loadSettings()` to also load FSR zeros and IMU offsets:
```cpp
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
```

- [ ] **Step 4: Call initSensors() in setup()**

In `setup()`, **after** `LittleFS.begin()` and **before** `loadSettings()`:
```cpp
  initSensors();
```

- [ ] **Step 5: Add a quick serial-based sensor smoke print**

Modify `loop()` temporarily to confirm sensors talk:
```cpp
void loop() {
  static uint32_t last = 0;
  if (millis() - last > 1000) {
    last = millis();
    readAllFsr();
    readImu();
    Serial.printf("[FSR] %d %d %d %d %d %d  [Acc] %.2f %.2f %.2f  [Gyr] %.2f %.2f %.2f\n",
      gFsr[0],gFsr[1],gFsr[2],gFsr[3],gFsr[4],gFsr[5],
      gAccel[0],gAccel[1],gAccel[2],
      gGyro[0],gGyro[1],gGyro[2]);
  }
  delay(1);
}
```

- [ ] **Step 6: Compile + flash, observe Serial Monitor**

Expected once per second:
```
[FSR] 0 0 0 0 0 0  [Acc] 0.05 -0.12 9.78  [Gyr] 0.10 -0.05 0.02
```

- If FSRs are unwired they'll read whatever the floating ADC gives -- that's fine for now.
- Acc Z should be ~9.8 (gravity, board flat) once IMU is wired.
- If Acc/Gyr are all zero or `nan`: I2C isn't talking. Check SDA/SCL wiring; confirm I2C address with an I2C scanner sketch.

**This is a screenshot opportunity** -- Serial Monitor showing live FSR + IMU data is a great slide.

- [ ] **Step 7: Revert the demo `loop()` to a stub**

We don't want this print interfering with sampling later. Replace the `loop()` body back with:
```cpp
void loop() {
  delay(1);
}
```

- [ ] **Step 8: Commit**

```bash
git add firmware/SoleSense/SoleSense.ino
git commit -m "feat(firmware): mux + MPU-6050 init and read primitives"
```

---

## Task 7: Calibration endpoints

**Files:**
- Modify: `firmware/SoleSense/SoleSense.ino`

- [ ] **Step 1: Add calibration routines and persistence helpers**

Add this section _between_ "Sensor reads" and "HTTP routes":
```cpp
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
```

- [ ] **Step 2: Add HTTP handlers**

Add to the HTTP routes section:
```cpp
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
```

In `setup()` route registration:
```cpp
  server.on("/api/calibrate/zero", HTTP_POST, handleCalibrateZero);
  server.on("/api/calibrate/imu",  HTTP_POST, handleCalibrateImu);
```

- [ ] **Step 3: Compile + flash**

- [ ] **Step 4: Smoke test**

```bash
# With insole unloaded:
curl -s -X POST http://192.168.4.1/api/calibrate/zero | python3 -m json.tool
# Expected: {"ok": true, "fsrZero": [<6 small ints>]}

# With insole flat:
curl -s -X POST http://192.168.4.1/api/calibrate/imu | python3 -m json.tool
# Expected: {"ok": true, "accel": [...], "gyro": [...]}
# accel[2] (z-axis) should be small after subtracting gravity, ~|0.5|

# Reset board, confirm persistence via /api/device or just rerun:
curl -s -X POST http://192.168.4.1/api/calibrate/zero
# Should still succeed; values may differ slightly run-to-run
```

- [ ] **Step 5: Commit**

```bash
git add firmware/SoleSense/SoleSense.ino
git commit -m "feat(firmware): FSR + IMU calibration with NVS persistence"
```

---

## Task 8: State machine + start/stop endpoints (no sampling yet)

**Files:**
- Modify: `firmware/SoleSense/SoleSense.ino`

This task wires the start/stop machinery so `gState` flips correctly and the file lifecycle works. Sampling lands in Task 9.

- [ ] **Step 1: Add request flags and file globals**

Add after the existing `volatile State gState = IDLE;` line:
```cpp
volatile bool gStartRequested = false;
volatile bool gStopRequested  = false;

File gFile;
```

- [ ] **Step 2: Add start/stop handlers**

Add to HTTP routes:
```cpp
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
```

- [ ] **Step 3: Add state-machine plumbing**

Add this section _before_ `setup()`:
```cpp
// =============================================================================
// State machine
// =============================================================================

static void enterRecording() {
  gFile = LittleFS.open("/data.csv", "w");
  if (!gFile) {
    Serial.println("[REC] open /data.csv FAILED");
    return;
  }
  gState = RECORDING;
  Serial.println("[REC] started");
}

static void exitRecording() {
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
}
```

- [ ] **Step 4: Drive the state machine from loop()**

```cpp
void loop() {
  processRequests();
  delay(1);
}
```

- [ ] **Step 5: Register routes in setup()**

```cpp
  server.on("/api/start", HTTP_POST, handleStart);
  server.on("/api/stop",  HTTP_POST, handleStop);
```

- [ ] **Step 6: Compile + flash**

- [ ] **Step 7: Smoke test**

```bash
# Idle to recording
curl -s -X POST http://192.168.4.1/api/start
# {"ok":true}

curl -s http://192.168.4.1/api/device | grep state
# "state": "recording"

# Conflict
curl -s -X POST http://192.168.4.1/api/start
# {"ok":false,"error":"already recording"}

# Stop
curl -s -X POST http://192.168.4.1/api/stop
# {"ok":true}

# /data.csv exists but empty (sampling not added yet)
curl -s http://192.168.4.1/api/device | grep hasData
# "hasData": true
```

Serial Monitor should show `[REC] started` / `[REC] stopped`.

- [ ] **Step 8: Commit**

```bash
git add firmware/SoleSense/SoleSense.ino
git commit -m "feat(firmware): IDLE/RECORDING state machine with /api/start and /api/stop"
```

---

## Task 9: 50 Hz hardware timer + ring buffer + CSV writer

**Files:**
- Modify: `firmware/SoleSense/SoleSense.ino`

- [ ] **Step 1: Add timer + ring-buffer globals**

Add after the `File gFile;` line:
```cpp
volatile bool gNewSample = false;
hw_timer_t* gTimer = nullptr;

static char     gRowBuf[25][96];
static uint8_t  gRowCount    = 0;
static uint32_t gLastFlushMs = 0;
```

- [ ] **Step 2: Add the ISR + timer control**

Add this section _after_ "State machine":
```cpp
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
```

- [ ] **Step 3: Add buffered CSV writer + takeSample**

Add this section _after_ the timer block:
```cpp
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
```

- [ ] **Step 4: Hook timer into state transitions**

Update `enterRecording()` and `exitRecording()`:
```cpp
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
```

- [ ] **Step 5: Drive sampling from loop()**

```cpp
void loop() {
  processRequests();
  if (gState == RECORDING && gNewSample) {
    gNewSample = false;
    takeSample();
  }
  delay(1);
}
```

- [ ] **Step 6: Compile + flash**

- [ ] **Step 7: Smoke test -- 5-second recording**

```bash
curl -s -X POST http://192.168.4.1/api/start
sleep 5
curl -s -X POST http://192.168.4.1/api/stop
curl -s http://192.168.4.1/data.csv | head -5
curl -s http://192.168.4.1/data.csv | wc -l
```

Expected:
- First 5 lines: `<timestamp_ms>,<6 ints>,<6 floats>` rows, ~20 ms apart
- Total line count: ~250 (5 seconds x 50 Hz). Tolerable range: 240-260.
- `/api/device` -> `usedBytes` grew by ~24 KB.

**Screenshot opportunity:** terminal output showing live CSV rows.

- [ ] **Step 8: Commit**

```bash
git add firmware/SoleSense/SoleSense.ino
git commit -m "feat(firmware): 50 Hz sampling with ring buffer and CSV writer"
```

---

## Task 10: GET /data.csv + POST /api/data/clear

**Files:**
- Modify: `firmware/SoleSense/SoleSense.ino`

- [ ] **Step 1: Add /data.csv handler**

Add to HTTP routes:
```cpp
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
```

In `setup()` route registration:
```cpp
  server.on("/data.csv",       HTTP_GET,  handleDataCsv);
  server.on("/api/data/clear", HTTP_POST, handleDataClear);
```

- [ ] **Step 2: Compile + flash**

- [ ] **Step 3: Smoke test**

```bash
# Download CSV from a recent run
curl -s http://192.168.4.1/data.csv | head -3

# Clear it
curl -s -X POST http://192.168.4.1/api/data/clear
# {"ok":true}

# Verify it's gone
curl -s http://192.168.4.1/api/device | grep hasData
# "hasData": false

curl -s http://192.168.4.1/data.csv
# no data

# Conflict path
curl -s -X POST http://192.168.4.1/api/start
curl -s -X POST http://192.168.4.1/api/data/clear
# {"ok":false,"error":"recording"}
curl -s -X POST http://192.168.4.1/api/stop
```

- [ ] **Step 4: Commit**

```bash
git add firmware/SoleSense/SoleSense.ino
git commit -m "feat(firmware): GET /data.csv and POST /api/data/clear"
```

---

## Task 11: Deep sleep + GPIO9 wake button

**Files:**
- Modify: `firmware/SoleSense/SoleSense.ino`

- [ ] **Step 1: Add sleep request flag + handler**

Add the flag near the other request flags:
```cpp
volatile bool gSleepRequested = false;
```

Add the HTTP handler in HTTP routes:
```cpp
static void handleSleep(AsyncWebServerRequest* req) {
  if (gState == RECORDING) {
    req->send(409, "application/json", "{\"ok\":false,\"error\":\"recording\"}");
    return;
  }
  gSleepRequested = true;
  req->send(200, "application/json", "{\"ok\":true}");
}
```

In route registration:
```cpp
  server.on("/api/sleep", HTTP_POST, handleSleep);
```

- [ ] **Step 2: Configure wake pin in setup()**

Add to `setup()`, after `initSensors()`:
```cpp
  pinMode(PIN_WAKE, INPUT_PULLUP);
```

- [ ] **Step 3: Add deep-sleep entry**

Add this section _after_ "Sample loop + CSV writer":
```cpp
// =============================================================================
// Deep sleep
// =============================================================================

static void enterDeepSleep() {
  Serial.println("[Sleep] entering deep sleep, wake on GPIO9 LOW");
  Serial.flush();
  delay(150);                              // let HTTP response flush

  WiFi.softAPdisconnect(true);
  LittleFS.end();

  esp_deep_sleep_enable_gpio_wakeup(1ULL << PIN_WAKE, ESP_GPIO_WAKEUP_GPIO_LOW);
  esp_deep_sleep_start();                  // never returns
}
```

- [ ] **Step 4: Wire request flag into loop()**

Update `processRequests()`:
```cpp
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
```

- [ ] **Step 5: Compile + flash**

- [ ] **Step 6: Smoke test**

```bash
curl -s -X POST http://192.168.4.1/api/sleep
# {"ok":true}
```

Expected:
- Phone WiFi shows `SoleSense` disappear within ~1 s
- Serial Monitor (if attached): `[Sleep] entering deep sleep, wake on GPIO9 LOW`, then silence
- Press the on-board BOOT button -> board reboots, SSID returns in ~1 s

**Note for the demo:** the BOOT button is the small push button on the XIAO board itself -- don't unplug USB to "wake". On battery you'd press it; on USB the board re-enumerates anyway.

- [ ] **Step 7: Commit**

```bash
git add firmware/SoleSense/SoleSense.ino
git commit -m "feat(firmware): deep sleep with GPIO9 wake"
```

---

## Task 12: End-to-end smoke test against the test panel

**Files:** none -- verification only.

- [ ] **Step 1: Confirm all 10 routes are registered**

Look at the route registration block in `setup()`. There must be exactly these 10 registrations (plus the `serveStatic` line for `/`):
```
server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
server.on("/data.csv",            HTTP_GET,  handleDataCsv);
server.on("/api/start",           HTTP_POST, handleStart);
server.on("/api/stop",            HTTP_POST, handleStop);
server.on("/api/calibrate/zero",  HTTP_POST, handleCalibrateZero);
server.on("/api/calibrate/imu",   HTTP_POST, handleCalibrateImu);
server.on("/api/settings",        HTTP_POST, handleSettings);
server.on("/api/data/clear",      HTTP_POST, handleDataClear);
server.on("/api/sleep",           HTTP_POST, handleSleep);
server.on("/api/device",          HTTP_GET,  handleDevice);
```

If any are missing, add them and re-flash.

- [ ] **Step 2: Run the full curl sequence from a laptop**

```bash
BASE=http://192.168.4.1

curl -s -X POST $BASE/api/data/clear
curl -s -X POST $BASE/api/calibrate/zero | python3 -m json.tool
curl -s -X POST $BASE/api/calibrate/imu  | python3 -m json.tool
curl -s -X POST $BASE/api/start
sleep 10
curl -s -X POST $BASE/api/stop
curl -s $BASE/api/device | python3 -m json.tool
curl -s $BASE/data.csv | head -10
curl -s $BASE/data.csv | wc -l   # expect ~500
curl -s -X POST -d 'cadenceMin=170' $BASE/api/settings
curl -s $BASE/api/device | python3 -m json.tool | grep cadenceMin
```

All should respond cleanly.

- [ ] **Step 3: Browser-based test from phone**

Phone -> connect to `SoleSense` -> open `http://192.168.4.1/`.

Run through the test panel buttons:
1. Tap `/api/device` -> JSON in the pre block
2. Tap `FSR zero` (with insole unloaded) -> success JSON
3. Tap `IMU zero` (with insole flat) -> success JSON
4. Tap `Start Run` -> success JSON
5. Wait 5-10 s
6. Tap `Stop Run` -> success JSON
7. Tap `Download CSV` -> CSV opens or downloads in browser
8. Tap `Clear data` -> success JSON

**Screenshots for slides:**
- Phone WiFi list with `SoleSense`
- `/api/device` JSON in test panel
- A snippet of the CSV
- Serial Monitor showing `[REC] started` / `[REC] stopped`

- [ ] **Step 4: Commit (touch only -- no code change)**

Create a tag for the demo build:
```bash
git tag v0.1-demo -m "v0.1 firmware skeleton -- full backend API working"
```
(Push the tag later if you want it on GitHub: `git push origin v0.1-demo`.)

---

## Task 13: Drop in real frontend (when ready)

**Files:**
- Replace: `firmware/SoleSense/data/index.html` with the production SPA

- [ ] **Step 1: Place the real index.html in the data folder**

Copy your real frontend file to `firmware/SoleSense/data/index.html`, overwriting the placeholder. The file must be self-contained (no CDN, no external assets) per `SOLESENSE.md` 5.

If the real SPA expects 7 FSR columns, update the JS analysis pipeline first to expect 6 columns (per the recent hardware change):
- CSV columns are now `timestamp_ms, fsr1..fsr6, accel_x/y/z, gyro_x/y/z` (13 cols, not 14)
- L/R balance and zone-average code that loops `for i in [0..6]` should be `[0..5]`
- Heel-strike detection still keys on FSR column 1 (heel)

- [ ] **Step 2: Re-upload LittleFS**

Close Serial Monitor. Arduino IDE -> `Cmd+Shift+P` -> `Upload LittleFS to Pico/ESP8266/ESP32` -> Enter.

- [ ] **Step 3: Verify the real UI loads**

Phone -> connect to `SoleSense` -> open `http://192.168.4.1/`. The real SPA should render.

- [ ] **Step 4: Run the full Start -> Stop -> Report flow**

1. Tap `Start Run`
2. Walk/run for 30+ seconds (or simulate with hand pressure on FSRs)
3. Tap `Stop`
4. Confirm the report renders with real cadence, GCT, balance, and any flags

**Screenshot for slides:** the report screen with real data.

- [ ] **Step 5: Commit**

```bash
git add firmware/SoleSense/data/index.html
git commit -m "feat(firmware): integrate real SoleSense SPA frontend"
git push origin main
```

---

## Self-Review (already performed)

Spec coverage check against `2026-05-04-solesense-firmware-design.md`:

| Spec section | Implemented in |
|---|---|
| 1 Scope (10 endpoints + sampling + cal + sleep) | Tasks 2-11 |
| 2 File layout | Task 1 |
| 3 Concurrency model (request flags, single-task FS) | Tasks 8, 9, 11 |
| 4 Pin assignments (incl. GPIO9 wake) | Tasks 6, 11 |
| 5 Sensor reads (mux + IMU) | Task 6 |
| 6 Calibration (32 x 6 FSR, 64 x IMU) | Task 7 |
| 7 Ring buffer + CSV writer | Task 9 |
| 8 All 10 HTTP routes | Tasks 4, 5, 7, 8, 10, 11 |
| 9 NVS Preferences | Tasks 5, 6 (load), 7 (calibration save) |
| 10 Deep sleep + wake | Task 11 |
| 11 Error handling | inline 409 / 400 / 404 / 500 in handlers |
| 12 Boot sequence + banner | Tasks 1-6 cumulatively |
| 13 Testing plan | Task 12 |

No placeholders, no TODOs, every code block is complete. Method names consistent across tasks (`enterRecording`, `exitRecording`, `processRequests`, `takeSample`, `flushRowBuffer`, `startSampleTimer`, `stopSampleTimer`, `enterDeepSleep`).

---

## Demo escape hatch (if you run out of time)

Stopping points that still produce a slide-able demo:

| Stop after | What you can show |
|---|---|
| Task 2 | Phone connects to `SoleSense` AP |
| Task 4 | `/api/device` JSON in browser |
| Task 6 | Serial Monitor showing live FSR + IMU values |
| Task 9 | Real CSV downloaded with 50 Hz timestamps |
| Task 12 | Full backend smoke test, all 10 endpoints working |
| Task 13 | Full UI with real report |

Tasks 1-9 give you the headline demo. Tasks 10-11 are polish. Task 13 unlocks the real UI.
