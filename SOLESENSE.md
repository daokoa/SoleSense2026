# SoleSense
### 3D Printed Smart Running Insole — Biomechanical Analysis System
**SoleSense team · UCI · Spring 2026**
Built by Dao Doan and the SoleSense team.

---

## Table of Contents
1. [Project Overview](#1-project-overview)
2. [Team](#2-team)
3. [Hardware](#3-hardware)
4. [Firmware](#4-firmware)
5. [Frontend](#5-frontend)
6. [Data Pipeline](#6-data-pipeline)
7. [Injury Flags](#7-injury-flags)
8. [Sampling & Storage Strategy](#8-sampling--storage-strategy)
9. [Power & Sleep](#9-power--sleep)
10. [API Reference](#10-api-reference)
11. [File Structure](#11-file-structure)
12. [Setup & Flashing Guide](#12-setup--flashing-guide)
13. [Research Basis](#13-research-basis)
14. [Roadmap](#14-roadmap)

---

## 1. Project Overview

SoleSense is a self-contained biomechanical analysis insole that records pressure and motion data during a run and serves a single-page web report over a local WiFi access point — no internet, no app install, no cloud.

**Core loop:**
1. User powers on insole → ESP32-C3 boots, starts WiFi AP `SoleSense`
2. User connects phone to AP → opens `http://192.168.4.1`
3. Taps **Start Run** → firmware records at 50Hz to LittleFS as CSV
4. Taps **Stop** → frontend fetches CSV, runs JS analysis pipeline
5. Report screen shows cadence, ground contact time, pronation, L/R balance, and up to 7 injury risk flags with plain-English corrective actions

Everything runs on-device. No external dependencies at runtime.

---

## 2. Team

| Name | Role |
|---|---|
| Dao Doan | Firmware / Software |
| Andony Velasquez | Software Lead |
| Jasmine Dhaliwal | Software |
| Natalie Dai | Software |
| Norton Hoang | Electrical & Firmware Lead |
| Maxim Varakuta | Electrical & Firmware |
| Jordan Chan | Mechanical Lead |
| Kiara Peters | Mechanical |
| James Kim | Mechanical |
| Daniel Grivennikov | Mechanical |
| Ethan Kim | Mechanical |

---

## 3. Hardware

### MCU
- **Seeed XIAO ESP32-C3**
  - 4MB flash (2MB app / 1.5MB LittleFS partition)
  - 400KB SRAM
  - WiFi 802.11 b/g/n
  - USB-C, Arduino-compatible

### Pressure Sensing
- **6× FSR 402** force sensing resistors, **no multiplexer** — wired in two sets of three (each set time-multiplexed via a digital power line)
- 2 digital power pins (`GPIO5`, `GPIO10`) — one per set
- 3 shared analog inputs (`GPIO2/A0`, `GPIO3`, `GPIO4`) — analog A, B, C
- 12-bit ADC resolution → 0–4095 per sensor
- Per-FSR wiring: pin 1 → digital power for that set; pin 2 → analog input AND through a 10 kΩ pull-down resistor to GND (voltage divider)
- Read sequence per sample: power Set 1 HIGH (Set 2 high-Z) → read ADC A/B/C → power Set 2 HIGH (Set 1 high-Z) → read ADC A/B/C → both high-Z

| FSR | Set / Position | Reads on | Zone |
|---|---|---|---|
| 1A | Set 1, slot A | ADC A (`GPIO2`) | Heel |
| 1B | Set 1, slot B | ADC B (`GPIO3`) | Lateral Mid |
| 1C | Set 1, slot C | ADC C (`GPIO4`) | Medial Mid |
| 2A | Set 2, slot A | ADC A (`GPIO2`) | Ball Lateral |
| 2B | Set 2, slot B | ADC B (`GPIO3`) | Ball Medial |
| 2C | Set 2, slot C | ADC C (`GPIO4`) | Toe 1 (hallux) |

### IMU
- **MPU-6050** 6-axis IMU on I2C
  - `SDA → GPIO6`, `SCL → GPIO7`
  - I2C address: `0x68` (AD0 low)
  - Accel range: ±2g → 16384 LSB/g → converted to m/s²
  - Gyro range: ±250°/s → 131 LSB/(°/s) → converted to °/s

### Power
- **2× LIR2450** 3.7V 120mAh rechargeable Li-ion coin cells, **wired in parallel** per insole
- ~240mAh total capacity
- Active draw (ESP32-C3 + WiFi AP): ~80–160mA → 90–180 min active
- Deep sleep draw: ~5µA → weeks of standby
- **Deep sleep + physical wake button** is the required power strategy

### Mechanical
- TPU 85A insole, 3D printed
- Snap-fit shell housing electronics
- Designed in Fusion 360

---

## 4. Firmware

### Stack
- **Language:** C++ Arduino
- **Board:** ESP32C3 Dev Module (Espressif ESP32 Arduino core v3.x)
- **Libraries:**
  - `ESPAsyncWebServer` by ESP32Async
  - `AsyncTCP` by ESP32Async
  - `MPU6050` by Electronic Cats
  - `LittleFS` (built into ESP32 Arduino core)

### Pin Definitions
```cpp
#define PIN_SDA       6
#define PIN_SCL       7
#define PIN_MUX_SIG   A0   // GPIO2
#define PIN_MUX_S0    D0   // GPIO3
#define PIN_MUX_S1    D1   // GPIO4
#define PIN_MUX_S2    D2   // GPIO5
```

### State Machine
```
IDLE  ──/api/start──▶  RECORDING  ──/api/stop──▶  IDLE
```

### Sampling Loop
- 50Hz hardware timer (ESP32 `timerBegin` / `timerAlarm`)
- Timer ISR sets `gNewSample` flag only — no work in ISR
- `loop()` checks flag, calls `takeSample()`
- `takeSample()` reads all 6 FSRs via mux + IMU via I2C → formats CSV row → writes to open file

### Buffered Writes
- 25-row RAM buffer, flush every 0.5 seconds
- Reduces flash writes from 50/sec to 2/sec
- Reduces wear and frees CPU during sensor reads

### Calibration
- **FSR zero:** average 32 samples per channel with no load → stored as `gFsrZero[]`, subtracted from all subsequent reads
- **IMU zero:** average 64 samples at rest → stores offsets for accel X/Y and (accel Z − 9.81) and all gyro axes

---

## 5. Frontend

### Architecture
- Single self-contained `index.html` — no CDN, no frameworks, no external assets
- Served from LittleFS by the firmware's `GET /` route
- Works fully offline on-device
- **Demo mode fallback:** if hostname is not `192.168.4.1`, all API calls are stubbed with generated data — UI works in any browser off-device

### 4 Screens

#### Home
- SoleSense wordmark nav
- Status pill (connected / demo mode)
- Bold headline
- **Start Run** pill button
- Last run summary cards (duration, cadence, flags, storage)

#### Recording
- 108px monospace timer
- Stat chips: cadence, GCT, L/R balance, sample count
- Animated FSR pressure bars (7 zones)
- Red **Stop Run** button

#### Report
- Summary cards: cadence, contact time, L/R balance, flag count
- Expandable injury flag cards (sorted high → medium → low)
- Metrics table
- Zone pressure bars (7 zones)

#### Settings
- FSR zero calibration button → `POST /api/calibrate/zero`
- IMU calibration button → `POST /api/calibrate/imu`
- Download CSV button → `GET /data.csv`
- Clear data button → `POST /api/data/clear`
- Device info panel → `GET /api/device`

### Design System
- Background: `#ffffff`
- Accent: `#2563eb` (blue)
- Danger: `#ef4444` (red)
- Border radius: `18px`
- Typography: Syne (headings) + DM Sans (body) + DM Mono (numbers)
- Bubbly, modern, mobile-first

---

## 6. Data Pipeline

> **v0.2 in progress:** the raw-CSV-to-browser pipeline below is the v0.1 implementation. The team's plan for v0.2 moves all run state onto the MCU (FFT coefficients + outlier buffer in RAM, periodic multi-slot flash flushes). The browser will hold no state. Full design: [`docs/superpowers/specs/2026-05-06-v0.2-data-architecture.md`](docs/superpowers/specs/2026-05-06-v0.2-data-architecture.md).

### CSV Format (50Hz, 13 columns)
```
timestamp_ms, fsr1, fsr2, fsr3, fsr4, fsr5, fsr6,
accel_x, accel_y, accel_z, gyro_x, gyro_y, gyro_z
```

- `timestamp_ms` — `millis()` since boot
- `fsr1–6` — ADC counts 0–4095, zero-offset applied (channels 0–5 of mux: heel, lateral mid, medial mid, ball lateral, ball medial, toe 1)
- `accel_x/y/z` — m/s², calibration offset applied, gravity on Z
- `gyro_x/y/z` — °/s, calibration offset applied

### JS Analysis Pipeline (runs in browser after stop)

```
CSV text
  → parse rows
  → step detection (heel FSR peak detection, min gap 15 frames)
  → cadence (steps/duration × 60)
  → ground contact time (contact frames / total frames × duration / steps)
  → loading rate (max dForce/dt across all rows)
  → pronation (gyro_x integration over stance, averaged per step)
  → L/R balance (sum left zones vs right zones across the 6 FSRs)
  → zone averages (6 zones normalized 0–100%)
  → evaluate 7 injury flags
  → sort flags high → medium → low
  → render report
```

---

## 7. Injury Flags

| Flag | Severity | Threshold | Detection Method |
|---|---|---|---|
| Heel Striking | High | Heel FSR > 1500 ADC at first contact | Peak value at step onset |
| High Loading Rate | High | > 100 ADC/ms | Max dForce/dt |
| Low Cadence | Medium | < 160 spm | Steps / duration × 60 |
| Overpronation | High | > 15° | Gyro X integration per step |
| Supination | Medium | < −8° | Gyro X integration per step |
| Bilateral Asymmetry | Medium | > 10% L/R difference | Sum left vs right FSR zones |
| Long Ground Contact | Low | > 300ms | Contact frames per step |

All thresholds are peer-reviewed. Sources: Heiderscheit et al. (2011), Lieberman et al. (2010), Winter (2009), Antonsson & Mann (1985).

Each flag card shows:
- Severity badge (High / Medium / Low)
- Plain-English description of the issue
- Specific corrective action

---

## 8. Sampling & Storage Strategy

### Why 50Hz
- Nyquist theorem: ground reaction force signals contain meaningful biomechanical content up to ~20Hz → minimum 40Hz required to avoid aliasing
- 50Hz is the accepted clinical standard (Antonsson & Mann 1985, Winter 2009)
- FSR 402 response time ~1ms — above 50Hz you capture insole vibration noise, not biomechanical signal
- Commercial reference: Pedar-X (Novel GmbH) outputs at 50Hz; Moticon OpenGO logs at 25Hz

### Why Not 100Hz
- All 7 injury flags operate on step-level aggregates (50–300ms events)
- Most demanding flag (heel strike) resolves over ~50ms → 20Hz sufficient
- 100Hz doubles storage consumption with no clinical benefit for running under 6 m/s

### Storage Math (13-col CSV, ~90 chars/row)

| Strategy | Write rate | KB/min | 1.5MB endurance |
|---|---|---|---|
| Raw CSV 50Hz | 50 rows/sec | 270 KB/min | ~5.5 min |
| Raw CSV 50Hz buffered | 2 flushes/sec | 270 KB/min | ~5.5 min |
| Averaged CSV 5Hz | 5 rows/sec | 27 KB/min | ~55 min |
| Averaged + peaks 5Hz | 5 rows/sec | 36 KB/min | ~40 min |

### Recommended: 50Hz internal, 5Hz averaged writes with peak preservation
- Sample at 50Hz into RAM
- Average every 10 samples → write 1 row at 5Hz
- Also store `fsr_peak[6]` and `gyro_peak_x` per window
- 40+ minutes recording on 1.5MB partition
- Averaging acts as a free low-pass filter — removes ADC jitter, mux switching transients, TPU material vibration
- Validated by Choi et al. (2024, *Sensors*): averaged FSR data improves downstream GRF/CoP prediction accuracy

### Partition Scheme
Arduino IDE → Tools → Partition Scheme:
```
Default 4MB with spiffs (1.2MB APP / 1.5MB SPIFFS)
```

---

## 9. Power & Sleep

### Strategy: Deep Sleep + Physical Wake Button

**Why:**
- 2× LIR2450 in parallel = ~240mAh
- Active + WiFi: 80–160mA → 90–180 min if always on
- Deep sleep: ~5µA → weeks of standby
- Without sleep, insole is dead before the user puts it on if left powered

**Flow:**
```
Physical button press
  → ESP32-C3 wakes from deep sleep (~1 second boot)
  → WiFi AP starts
  → User connects, opens 192.168.4.1
  → Records run
  → Taps "Power Off" in UI → POST /api/sleep
  → ESP32-C3 enters deep sleep
```

**Hardware required:** 1 tactile button between a GPIO and GND (Norton/Jordan)

**Firmware:** `esp_deep_sleep_start()` with `esp_sleep_enable_ext0_wakeup(GPIO, 0)` — ~10 lines

---

## 10. API Reference

All endpoints served by `AsyncWebServer` on port 80 at `192.168.4.1`.

| Method | Path | Description |
|---|---|---|
| `GET` | `/` | Serve `index.html` from LittleFS |
| `GET` | `/data.csv` | Stream raw CSV from LittleFS |
| `POST` | `/api/start` | Begin recording session |
| `POST` | `/api/stop` | End recording, flush file |
| `POST` | `/api/calibrate/zero` | Zero FSR baselines (32-sample average) |
| `POST` | `/api/calibrate/imu` | Zero IMU offsets (64-sample average) |
| `POST` | `/api/settings` | Update analysis thresholds |
| `POST` | `/api/data/clear` | Delete `/data.csv` from LittleFS |
| `GET` | `/api/device` | Device info, state, storage, thresholds |

### `/api/device` Response
```json
{
  "firmware": "SoleSense v0.1",
  "board": "XIAO ESP32-C3",
  "sampleRateHz": 50,
  "state": "idle",
  "fs": { "totalBytes": 1441792, "usedBytes": 8192 },
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

### `/api/settings` Body (form-urlencoded)
```
hlr=100&proneMax=15&proneMin=-8&gct=300&cadenceMin=160
```

---

## 11. File Structure

```
SoleSense/
├── SoleSense.ino          # Main firmware — all C++ Arduino code
└── data/
    └── index.html         # Full frontend SPA — uploaded to LittleFS
```

### LittleFS (on-device flash)
```
/index.html                # Served at GET /
/data.csv                  # Created on first recording
```

---

## 12. Setup & Flashing Guide

### Prerequisites
- Arduino IDE 2.3.8+
- Seeed XIAO ESP32-C3 connected via USB-C

### 1 — Board Setup
File → Preferences → Additional boards manager URLs:
```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```
Tools → Board → Boards Manager → install **esp32 by Espressif Systems**
Tools → Board → ESP32 Arduino → **ESP32C3 Dev Module**

### 2 — Board Settings
```
USB CDC On Boot: Enabled
Partition Scheme: Default 4MB with spiffs (1.2MB APP / 1.5MB SPIFFS)
Flash Size: 4MB (32Mb)
CPU Frequency: 160MHz
```

### 3 — Libraries
Sketch → Include Library → Manage Libraries:
- `ESPAsyncWebServer` by **ESP32Async**
- `AsyncTCP` by **ESP32Async**
- `MPU6050` by **Electronic Cats**

### 4 — Flash Firmware
Select port → Upload (`→` button)
If stuck on "Connecting…": hold BOOT button on XIAO during upload

### 5 — Install LittleFS Upload Plugin
```bash
# Download .vsix from:
# https://github.com/earlephilhower/arduino-littlefs-upload/releases

mkdir -p ~/Library/Arduino15/plugins
cp ~/Downloads/arduino-littlefs-upload-*.vsix ~/Library/Arduino15/plugins/
cd ~/Library/Arduino15/plugins
unzip arduino-littlefs-upload-*.vsix -d arduino-littlefs-upload
# Restart Arduino IDE
```

### 6 — Flash Filesystem
Place `index.html` in `SoleSense/data/index.html`
Arduino IDE → `Cmd+Shift+P` → **Upload LittleFS** → Enter

### 7 — Test
- Open Serial Monitor at 115200 baud
- Expected boot output:
```
=== SoleSense booting ===
[FS] Mounted — X / Y bytes used
[WiFi] AP 'SoleSense' up at 192.168.4.1
[HTTP] server started
```
- Connect phone to `SoleSense` WiFi (password: `solesense`)
- Open `http://192.168.4.1` → full UI loads
- Or test API: `curl http://192.168.4.1/api/device`

---

## 13. Research Basis

### Sampling Rate — 50Hz
- **Antonsson & Mann (1985)** *Journal of Biomechanics* — GRF signals bandwidth up to 20Hz, Nyquist requires 40Hz minimum
- **Winter (2009)** *Biomechanics of Human Movement* — 50Hz clinical standard for gait analysis
- **Rosenbaum & Becker (1997)** *Clinical Biomechanics* — above 50Hz captures material vibration, not biomechanical signal

### Injury Flag Thresholds
- **Heiderscheit et al. (2011)** *Medicine & Science in Sports & Exercise* — cadence < 160 spm, GCT > 300ms
- **Lieberman et al. (2010)** *Nature* — heel strike loading rate and impact transient
- **Souza (2016)** *Journal of Orthopaedic & Sports Physical Therapy* — overpronation > 15°, supination < −8°
- **Zifchock et al. (2006)** *Clinical Biomechanics* — bilateral asymmetry > 10%

### FSR Sensor Placement & Averaging
- **Choi et al. (2024)** *Sensors* (MDPI), "Calibrating Low-Cost Smart Insole Sensors with Recurrent Neural Networks for Accurate Prediction of Center of Pressure" — 6-FSR insole validated against F-Scan ($20K system); FSR data fed into an RNN/LSTM model improves GRF/CoP prediction accuracy by 30%+. SoleSense's 6-sensor layout matches their validated zone configuration. Supports 50Hz sampling and averaging strategy.
- **Claverie et al. (2016)** *Medical Engineering & Physics* — discrete sensor distribution for plantar pressure analysis

### Commercial Benchmarks
- **Pedar-X (Novel GmbH)** — 100Hz internal, 50Hz output
- **Moticon OpenGO** — 100Hz internal, 25Hz logged
- **Tekscan F-Scan** — 100Hz, $20,000+ system

---

## 14. Roadmap

### v0.1 — Current (Spring 2026 Demo)
- [x] Firmware skeleton — WiFi AP, LittleFS, AsyncWebServer, all 8 endpoints
- [x] Frontend SPA — 4 screens, demo mode, analysis pipeline, 7 injury flags
- [ ] 50Hz sampling with 25-row RAM buffer
- [ ] Deep sleep + wake button
- [ ] Averaged 5Hz writes with peak preservation
- [ ] Full sensor integration (FSRs + IMU wired to PCB)

### v0.2 — Post-Demo
- [ ] Binary storage format with CSV conversion endpoint
- [ ] Multi-session support (session separator in CSV, session picker in UI)
- [ ] Over-the-air (OTA) firmware updates
- [ ] Left/right insole pairing over ESP-NOW

### v1.0 — Future
- [ ] RNN/LSTM model trained on collected CSV data (per Choi et al. 2024)
- [ ] Real-time CoP trajectory visualization
- [ ] Cadence audio feedback via BLE to earbuds
- [ ] Mobile app wrapper

---

*SoleSense — SoleSense team, UCI Spring 2026*
