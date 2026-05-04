---
project: SoleSense
component: Firmware (ESP32-C3)
version: v0.1 skeleton
date: 2026-05-04
status: design — ready for implementation plan
---

# SoleSense Firmware Design — v0.1

C++ Arduino firmware skeleton for the Seeed XIAO ESP32-C3, implementing all 10 HTTP endpoints, 50 Hz sampling of 6 FSRs + MPU-6050 with buffered LittleFS writes, FSR + IMU calibration, NVS-backed settings, and physical-button deep-sleep.

This document is the firmware-side companion to `SOLESENSE.md`. Where the two disagree, this document wins for v0.1.

---

## 1. Scope

In scope for v0.1:
- WiFi AP, LittleFS mount, AsyncWebServer with all 10 endpoints
- 50 Hz hardware-timer sampling of 6 FSRs + MPU-6050 with 25-row RAM ring buffer (raw 13-column CSV, matches `SOLESENSE.md` §6 schema)
- FSR zero + IMU offset calibration, persisted to NVS
- Threshold settings, persisted to NVS
- Physical-button deep sleep + wake

Out of scope for v0.1 (deferred to v0.2):
- Averaged 5 Hz writes with peak preservation (§8 of `SOLESENSE.md`)
- Binary storage format
- Multi-session support
- OTA updates
- ESP-NOW pairing

The frontend `index.html` is not part of this design — it already exists and consumes the raw 13-column CSV defined in `SOLESENSE.md` §6. (The frontend's analysis pipeline will need a one-time update to read 6 FSR columns instead of 7; that's a frontend task, not firmware.)

---

## 2. File Layout

Single-file Arduino sketch matching `SOLESENSE.md` §11:

```
SoleSense/
├── SoleSense.ino     # ~700 lines, sectioned by banner comments
└── data/
    └── index.html    # frontend SPA (already designed, uploaded via LittleFS plugin)
```

`SoleSense.ino` sections in this order:
1. Includes + pin defs + globals + state enum
2. NVS settings load/save
3. Sensor reads (mux + IMU, calibration applied)
4. Calibration routines (FSR zero, IMU zero)
5. Sample loop + ring buffer + CSV writer
6. Hardware timer ISR + state-machine plumbing
7. HTTP route handlers (10 routes)
8. Deep sleep
9. `setup()` + `loop()`

Sections separated by `// ============================` banner comments.

---

## 3. Concurrency Model

Three execution contexts touch shared state:

| Context | Runs at | What it does |
|---|---|---|
| Hardware timer ISR | 50 Hz (every 20 ms) | Sets `volatile bool gNewSample = true`. Nothing else. |
| `loop()` (main task) | Continuous | Drains `gNewSample`, calls `takeSample()`, manages ring buffer, flushes file, processes request flags |
| AsyncWebServer task | Per HTTP request | Runs route handlers on its own FreeRTOS task |

To avoid mutex bugs between the HTTP task and the sample loop, **HTTP handlers do not touch sampling state directly**. They set `volatile` request flags:

```cpp
volatile bool gStartRequested = false;
volatile bool gStopRequested  = false;
volatile bool gSleepRequested = false;
```

`loop()` observes the flags, transitions state, and performs file/timer/sleep operations. All file I/O happens on a single task. No mutex needed for the recording path.

**Calibration is the exception.** Calibration handlers run synchronously inside the HTTP task — but only when `gState == IDLE`. Handlers return HTTP 409 Conflict otherwise, so the sampling path is guaranteed quiet. Calibration touches the same ADC/I²C resources as `takeSample()`; the IDLE gate makes that safe.

State enum:
```cpp
enum State { IDLE, RECORDING };
volatile State gState = IDLE;
```

---

## 4. Pin Assignments

From `SOLESENSE.md` §4, plus one new addition:

```cpp
#define PIN_SDA       6     // I²C — MPU-6050
#define PIN_SCL       7     // I²C — MPU-6050
#define PIN_MUX_SIG   A0    // GPIO2 — analog mux output
#define PIN_MUX_S0    D0    // GPIO3 — mux select bit 0
#define PIN_MUX_S1    D1    // GPIO4 — mux select bit 1
#define PIN_MUX_S2    D2    // GPIO5 — mux select bit 2
#define PIN_WAKE      9     // GPIO9 — on-board BOOT button, doubles as wake button (v0.1)
```

**Wake pin rationale:** `SOLESENSE.md` §9 calls for a tactile-to-GND wake button but does not pick a pin. The XIAO ESP32-C3 board includes an on-board BOOT button on GPIO9 that is already a momentary-to-GND switch. Reusing it for v0.1 means hardware (Norton/Jordan) does not need to add a button to demo deep sleep. If a dedicated button is wired later, change `PIN_WAKE`.

GPIO9 is configured `INPUT_PULLUP` in `setup()`. Active low.

---

## 5. Sensor Reads

### 5.1 FSR via CD74HC4051 mux

```cpp
int readFsr(uint8_t channel) {
  digitalWrite(PIN_MUX_S0, channel & 0x01);
  digitalWrite(PIN_MUX_S1, (channel >> 1) & 0x01);
  digitalWrite(PIN_MUX_S2, (channel >> 2) & 0x01);
  delayMicroseconds(10);                        // mux settle
  return analogRead(PIN_MUX_SIG) - gFsrZero[channel];
}
```

Loop channels 0..5, store into `int16_t gFsr[6]`. ~50 µs per channel × 6 = ~300 µs total. Mux channels 6 and 7 are unused.

Channel-to-zone mapping per `SOLESENSE.md` §3 (heel, lateral mid, medial mid, ball lateral, ball medial, toe 1).

### 5.2 MPU-6050 via I²C

400 kHz I²C. Burst-read 14 bytes from register `0x3B` (`ACCEL_XOUT_H` through `GYRO_ZOUT_L`):

```
[ax_h ax_l] [ay_h ay_l] [az_h az_l]   // accel int16
[t_h  t_l]                            // temp (discarded)
[gx_h gx_l] [gy_h gy_l] [gz_h gz_l]   // gyro int16
```

Convert to physical units after applying offsets:
- accel raw → m/s²: `(raw / 16384.0) * 9.80665 - gImuOffset[ax|ay|az]`
- gyro raw → °/s: `raw / 131.0 - gImuOffset[gx|gy|gz]`

~350 µs over I²C.

Total `takeSample()` budget: ~650 µs, well under the 20 ms timer window.

---

## 6. Calibration

### 6.1 FSR zero (`POST /api/calibrate/zero`)

Insole assumed unloaded. For each channel 0..5:
- Sample 32 times with ~1 ms spacing between samples (10 µs mux settle is already in `readFsr()`)
- Average → `gFsrZero[channel]` (int)

Persist all 6 zeros to NVS. Return as JSON:
```json
{ "ok": true, "fsrZero": [123, 118, 109, 142, 130, 121] }
```

### 6.2 IMU zero (`POST /api/calibrate/imu`)

Insole assumed flat and stationary. Sample 64 readings of all 6 axes (raw, no offset applied). Average each. Then:
- `gImuOffset[ax]` = mean(accel_x in m/s²)
- `gImuOffset[ay]` = mean(accel_y in m/s²)
- `gImuOffset[az]` = mean(accel_z in m/s²) − 9.80665   ← gravity stays on Z
- `gImuOffset[gx|gy|gz]` = mean(gyro in °/s)

Persist to NVS. Return offsets as JSON.

### 6.3 Persistence rationale

`SOLESENSE.md` does not specify whether calibration survives reboot. v0.1 persists both FSR zeros and IMU offsets to NVS. Reasoning: calibration is a deliberate user action ("take the insole off and tap Calibrate"). Re-doing it on every power cycle is friction. Easy to flip to RAM-only later if it causes drift problems.

---

## 7. Sample Loop, Ring Buffer, CSV Writer

### 7.1 Ring buffer

```cpp
static char     gRowBuf[25][96];    // 25 rows × ~90 chars = 2.4 KB RAM
static uint8_t  gRowCount = 0;
static uint32_t gLastFlushMs = 0;
```

### 7.2 `takeSample()` body

Called by `loop()` when `gNewSample` is set, only while `gState == RECORDING`:

1. Clear `gNewSample`.
2. Read all 6 FSRs (mux loop).
3. Read MPU-6050 (14-byte I²C burst, convert to m/s² + °/s, apply offsets).
4. `snprintf` one row into `gRowBuf[gRowCount++]`:
   ```
   "%lu,%d,%d,%d,%d,%d,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f\n"
   ```
   13 columns, matches `SOLESENSE.md` §6 schema exactly.
5. If `gRowCount >= 25` OR `(millis() - gLastFlushMs) >= 500`:
   - One `gFile.write()` of all queued rows in a single call
   - Reset `gRowCount = 0`, update `gLastFlushMs`

One `file.write()` per flush → ~2 syscalls/sec to LittleFS instead of 50.

### 7.3 Storage budget

~90 chars/row × 50 rows/sec = ~4.5 KB/sec. On the 1.5 MB partition that gives **~5.5 minutes** of recording. Matches `SOLESENSE.md` §8's updated 13-col storage table. Adequate for v0.1 demo runs. The averaged 5 Hz path lands in v0.2 to extend this to 40+ minutes.

### 7.4 Hardware timer

Use the ESP32 Arduino core 3.x API:
```cpp
hw_timer_t* gTimer = nullptr;
gTimer = timerBegin(1000000);              // 1 MHz tick
timerAttachInterrupt(gTimer, &onTimer);
timerAlarm(gTimer, 20000, true, 0);        // 20 ms period, autoreload
```

Timer is started by the IDLE→RECORDING transition and stopped by RECORDING→IDLE.

ISR body — minimum work, no I/O, no logging:
```cpp
void IRAM_ATTR onTimer() { gNewSample = true; }
```

---

## 8. HTTP Endpoints

`AsyncWebServer server(80)` on `192.168.4.1`. Started after all routes registered and after `LittleFS.begin()`.

WiFi AP: `WiFi.softAP("SoleSense", "solesense")`.

| Method | Path | Behavior | Errors |
|---|---|---|---|
| `GET` | `/` | `serveStatic("/", LittleFS, "/").setDefaultFile("index.html")` | 404 if not uploaded |
| `GET` | `/data.csv` | `request->send(LittleFS, "/data.csv", "text/csv")` (chunked) | 404 if no recording yet, 409 if RECORDING |
| `POST` | `/api/start` | If IDLE: `gStartRequested = true`, return `{"ok":true}` | 409 if already RECORDING |
| `POST` | `/api/stop` | If RECORDING: `gStopRequested = true`, return `{"ok":true}` | 409 if IDLE |
| `POST` | `/api/calibrate/zero` | Block 32 × 6 samples → `gFsrZero[]`, persist NVS, return offsets | 409 if RECORDING |
| `POST` | `/api/calibrate/imu` | Block 64 samples → `gImuOffset[6]`, persist NVS, return offsets | 409 if RECORDING |
| `POST` | `/api/settings` | Parse form-urlencoded, validate, update RAM thresholds, persist NVS | 400 on out-of-range |
| `POST` | `/api/data/clear` | `LittleFS.remove("/data.csv")`, return `{"ok":true}` | 409 if RECORDING |
| `POST` | `/api/sleep` | Return `{"ok":true}`, `gSleepRequested = true` | 409 if RECORDING |
| `GET` | `/api/device` | Return JSON device info | — |

### 8.1 `GET /api/device` response

Per `SOLESENSE.md` §10:
```json
{
  "firmware": "SoleSense v0.1",
  "board": "XIAO ESP32-C3",
  "sampleRateHz": 50,
  "state": "idle" | "recording",
  "fs": { "totalBytes": 1441792, "usedBytes": 8192 },
  "hasData": true | false,
  "thresholds": {
    "hlr": 100,
    "proneMax": 15,
    "proneMin": -8,
    "gct": 300,
    "cadenceMin": 160
  }
}
```

`fs` values from `LittleFS.totalBytes()` / `LittleFS.usedBytes()`.
`hasData` from `LittleFS.exists("/data.csv")`.

### 8.2 `POST /api/settings` body

Form-urlencoded, all params optional (only specified ones get updated):
```
hlr=100&proneMax=15&proneMin=-8&gct=300&cadenceMin=160
```

Parsed via `request->getParam("hlr", true)`. Validation ranges:

| Param | Range |
|---|---|
| `hlr` | 1–10000 |
| `proneMax` | 0–90 |
| `proneMin` | −90–0 |
| `gct` | 50–2000 |
| `cadenceMin` | 60–300 |

Out-of-range → 400 Bad Request, no thresholds updated. All-or-nothing per request.

---

## 9. NVS Settings (Preferences)

ESP32 Arduino built-in `Preferences` library. Single namespace `"solesense"`.

| Key | Type | Default | Source |
|---|---|---|---|
| `hlr` | int | 100 | threshold |
| `proneMax` | int | 15 | threshold |
| `proneMin` | int | −8 | threshold |
| `gct` | int | 300 | threshold |
| `cadenceMin` | int | 160 | threshold |
| `fsrZ0` … `fsrZ5` | int | 0 | FSR zero per channel |
| `imuOax`, `imuOay`, `imuOaz` | float | 0.0 | accel offset (m/s²) |
| `imuOgx`, `imuOgy`, `imuOgz` | float | 0.0 | gyro offset (°/s) |

Loaded once in `setup()` via `prefs.getInt(key, default)` / `prefs.getFloat(key, default)`. Written from `/api/settings`, `/api/calibrate/zero`, `/api/calibrate/imu`.

NVS write counts: thresholds change only when user taps Save (rare); calibration only on user tap (rare). Well within NVS endurance.

---

## 10. Deep Sleep + Wake

### 10.1 Sleep flow

```
POST /api/sleep
  → handler (HTTP task):
      if RECORDING       → 409 Conflict
      else               → set gSleepRequested = true, return {"ok":true}
  → loop() (next tick):
      observes flag
      delay(150 ms)                         ← lets TCP flush + AP shutdown gracefully
      WiFi.softAPdisconnect(true)
      LittleFS.end()
      esp_deep_sleep_start()                ← never returns
```

The 150 ms grace prevents `esp_deep_sleep_start()` from firing before the `{"ok":true}` packet hits the wire. Without it, the user's browser sees a connection reset instead of a clean response.

### 10.2 Wake configuration

ESP32-C3 does not support classic `ext0` wake. Use the deep-sleep GPIO wakeup API:

```cpp
esp_deep_sleep_enable_gpio_wakeup(BIT(PIN_WAKE), ESP_GPIO_WAKEUP_GPIO_LOW);
```

Active low. `PIN_WAKE = GPIO9` configured `INPUT_PULLUP` in `setup()`.

### 10.3 Wake-side behavior

Boot from deep sleep is indistinguishable from cold boot for v0.1 — `setup()` runs unconditionally, AP comes up at `192.168.4.1` in ~1 second. No `esp_sleep_get_wakeup_cause()` inspection needed.

---

## 11. Error Handling

| Failure | Response |
|---|---|
| `LittleFS.begin()` fails in `setup()` | Log to Serial. Continue without filesystem. `/api/device` reports zero `fs` bytes. `GET /` returns 404. Recording endpoints return 500. |
| `timerBegin()` returns null | Log. `/api/start` returns 500. |
| `gFile = LittleFS.open("/data.csv", "w")` fails on start | Abort transition, log, set RECORDING flag back to IDLE, `/api/start` returns 500. |
| HTTP handler fires while in wrong state | 409 Conflict with `{"ok":false,"error":"<reason>"}` |
| Bad form body to `/api/settings` | 400 Bad Request with `{"ok":false,"error":"<param> out of range"}` |

Logging is to `Serial` at 115200 baud, matching the boot-output expectations in `SOLESENSE.md` §12 step 7.

---

## 12. Boot Sequence

`setup()` order:
1. `Serial.begin(115200)` (USB CDC)
2. Pin modes (mux selects, wake button as `INPUT_PULLUP`)
3. `Wire.begin(PIN_SDA, PIN_SCL); Wire.setClock(400000);`
4. MPU-6050 init: write `PWR_MGMT_1 = 0` (wake), `CONFIG = 0`, `GYRO_CONFIG = 0`, `ACCEL_CONFIG = 0`
5. ADC: `analogReadResolution(12)`
6. `LittleFS.begin()`
7. `prefs.begin("solesense", false)` → load thresholds, FSR zeros, IMU offsets
8. `WiFi.softAP("SoleSense", "solesense")`
9. Register all 10 HTTP routes on `server`
10. `server.begin()`
11. Print boot banner matching `SOLESENSE.md` §12:
    ```
    === SoleSense booting ===
    [FS] Mounted — X / Y bytes used
    [WiFi] AP 'SoleSense' up at 192.168.4.1
    [HTTP] server started
    ```

`loop()` body:
```cpp
void loop() {
  if (gStartRequested) handleStart();
  if (gStopRequested)  handleStop();
  if (gSleepRequested) handleSleep();   // never returns
  if (gState == RECORDING && gNewSample) takeSample();
  delay(1);
}
```

---

## 13. Testing Plan

Arduino code is hard to unit-test. Verification path for v0.1:

1. **Compile + upload** to XIAO ESP32-C3. Confirm boot banner on Serial Monitor.
2. **AP visibility:** phone sees `SoleSense` SSID, connects with password `solesense`.
3. **Endpoint smoke test** from a laptop on the AP:
   ```
   curl http://192.168.4.1/api/device
   curl -X POST http://192.168.4.1/api/calibrate/zero
   curl -X POST http://192.168.4.1/api/start
   sleep 10
   curl -X POST http://192.168.4.1/api/stop
   curl http://192.168.4.1/data.csv | head
   ```
   Expect: clean JSON, then 13-column CSV with ~500 rows for a 10-second run.
4. **Frontend integration:** open `http://192.168.4.1/` on phone, full Start → Stop → Report flow against real CSV.
5. **Error states:** `POST /api/start` while recording → 409. `POST /api/settings` with `cadenceMin=999` → 400.
6. **Deep sleep:** `POST /api/sleep` while idle, observe board goes silent (~5 µA on a multimeter), press BOOT button, AP comes back in ~1 s.

No automated tests in v0.1. CI runs the compile step only (if/when CI exists for this repo).

---

## 14. Open Questions Deferred to v0.2

- Averaged 5 Hz writes with peak preservation (`SOLESENSE.md` §8 recommendation). Requires CSV schema change and frontend analysis-pipeline update.
- Binary storage format with CSV-conversion endpoint.
- Multi-session support (session separator + picker).
- ESP-NOW left/right pairing.
- OTA firmware updates.

These are listed in `SOLESENSE.md` §14 v0.2 and remain there.
