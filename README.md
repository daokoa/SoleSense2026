# SoleSense

**3D Printed Smart Running Insole — Biomechanical Analysis System**
*SoleSense team · legacy · UCI · Spring 2026*

---

## Status

**v0.1 firmware deployed and verified on hardware.** XIAO ESP32-C3 boots, hosts a WiFi access point, serves the frontend SPA from LittleFS, and exposes a 10-endpoint HTTP API. End-to-end frontend + backend integration verified (page loads on phone, polls live sensor endpoint).

What's not yet wired up:
- FSR pressure sensors and CD74HC4051 multiplexer (parts pending)
- IMU connected to XIAO (parts in hand, soldering pending)
- Final 3D-printed TPU shell

See **[Roadmap](#roadmap)** for the path to v1.0.

---

## What SoleSense Does

A self-contained biomechanical analysis insole that records pressure and motion data during a run and serves a single-page web report over its own WiFi access point — no internet, no app install, no cloud.

**Core loop:**
1. Power on → ESP32-C3 boots, starts WiFi AP `SoleSense`
2. User connects phone to AP → opens `http://192.168.4.1`
3. Taps **Start Run** → firmware records at 50 Hz to LittleFS as CSV
4. Taps **Stop** → frontend fetches CSV, runs JS analysis pipeline
5. Report screen shows cadence, ground contact time, pronation, L/R balance, and up to 7 injury risk flags

---

## Detected Injury Patterns

Sourced from peer-reviewed biomechanics literature (full citations in [`SOLESENSE.md`](SOLESENSE.md) §13):

- Heel striking
- High loading rate
- Low cadence
- Overpronation
- Supination
- Bilateral asymmetry
- Long ground contact time

---

## Hardware

| Component | Role | Status |
|---|---|---|
| Seeed XIAO ESP32-C3 | MCU — reads sensors, hosts WiFi AP, serves SPA | in hand, flashed |
| MPU-6050 | 3-axis accel + 3-axis gyro (I²C) | in hand, soldering pending |
| FSR 402 × 6 | Pressure sensors (heel, lateral/medial mid, lateral/medial ball, toe 1) | pending |
| CD74HC4051 | 8-channel analog mux to read all 6 FSRs from 1 ADC pin | pending |
| LIR2450 × 2 in parallel | 3.7 V 120 mAh Li-ion coin cells (~240 mAh combined) | pending |
| TPU 85A filament | 3D-printed insole shell, gyroid 20–25% infill | pending |

---

## Pin Map

```
XIAO ESP32-C3 → MPU-6050 (I²C):
  D4 (GPIO6)  → SDA
  D5 (GPIO7)  → SCL
  3V3         → VCC
  GND         → GND
  GND         → AD0   (sets I²C address to 0x68)

XIAO → CD74HC4051 mux:
  D0 (GPIO2/A0) → Y    (analog signal in)
  D1 (GPIO3)    → S0
  D2 (GPIO4)    → S1
  D3 (GPIO5)    → S2
  3V3           → VCC
  GND           → GND, INH, VEE

Mux Y0..Y5 → 6 FSRs (each in a 10kΩ voltage divider to GND):
  Y0 → Heel
  Y1 → Lateral Mid
  Y2 → Medial Mid
  Y3 → Ball Lateral
  Y4 → Ball Medial
  Y5 → Toe 1
  Y6, Y7 → unused

Wake button:
  GPIO9 — uses the on-board BOOT button on the XIAO; no extra hardware
```

Pin defines live at the top of [`firmware/SoleSense/SoleSense.ino`](firmware/SoleSense/SoleSense.ino).

---

## API

All endpoints served at `http://192.168.4.1` once connected to the `SoleSense` WiFi AP.

| Method | Path | Purpose |
|---|---|---|
| `GET` | `/` | Serves the frontend SPA from LittleFS |
| `GET` | `/api/device` | Device info: firmware, board, state, fs bytes, hasData, thresholds |
| `GET` | `/api/sensor` | Live FSR + IMU snapshot (frontend polls this) |
| `POST` | `/api/start` | Begin server-side 50 Hz recording to `/data.csv` |
| `POST` | `/api/stop` | End recording, flush + close file |
| `GET` | `/data.csv` | Stream the recorded CSV (409 while recording) |
| `POST` | `/api/data/clear` | Delete `/data.csv` |
| `POST` | `/api/calibrate/zero` | Zero the 6 FSRs (insole unloaded), persist to NVS |
| `POST` | `/api/calibrate/imu` | Zero accel + gyro offsets (insole flat), persist to NVS |
| `POST` | `/api/settings` | Update injury-flag thresholds (validated, persisted) |
| `POST` | `/api/sleep` | Enter deep sleep; wake on GPIO9 LOW |

CSV schema (13 columns, 50 Hz):
```
timestamp_ms, fsr1..fsr6, accel_x, accel_y, accel_z, gyro_x, gyro_y, gyro_z
```

---

## Repository Structure

```
solesense/
├── README.md                         ← you are here
├── SOLESENSE.md                      ← canonical project spec
├── .gitignore
│
├── firmware/                         ← BOTH firmware paths grouped, see firmware/README.md
│   ├── README.md                     ← explains arduino-ide vs platformio choice
│   ├── SoleSense/                    ← (Arduino IDE) WORKING firmware
│   │   ├── SoleSense.ino             ← v0.1 firmware, ~520 lines, flashed and verified
│   │   └── data/
│   │       └── index.html            ← canonical frontend SPA (LittleFS source)
│   └── platformio/                   ← (PlatformIO) parallel stub firmware
│       ├── platformio.ini
│       ├── src/main.cpp              ← Andony's stub: dummy data, OTA, alt SSID
│       ├── include/
│       ├── lib/
│       └── test/
│
├── data/                             ← duplicate of firmware/SoleSense/data/, see data/README.md
│   ├── README.md
│   └── index.html
│
├── software/
│   └── frontend/                     ← reserved for future component-based frontend (empty)
│
├── hardware/
│   ├── cad/FSR Cutout.SLDPRT         ← SolidWorks CAD
│   ├── electrical/Solesense_WD.*     ← KiCad schematic + PCB
│   └── pcb/                          ← reserved for production PCB files (empty)
│
├── docs/
│   ├── pseudocode/                   ← BOTH pseudocode files grouped, see docs/pseudocode/README.md
│   │   ├── README.md
│   │   ├── system-flow.md            ← high-level system flow (137 lines)
│   │   └── injury-analysis.md        ← detailed injury-flag algorithms (437 lines)
│   └── superpowers/
│       ├── specs/                    ← v0.1 firmware design doc
│       └── plans/                    ← v0.1 implementation plan
│
└── assets/                           ← images, diagrams (reserved, empty)
```

### Note on the two firmwares

Both live under [`firmware/`](firmware/) — see [`firmware/README.md`](firmware/README.md) for the breakdown. TL;DR:

- **`firmware/SoleSense/SoleSense.ino`** (Arduino IDE) — the working v0.1 firmware. ~520 lines. All 10 endpoints, 50 Hz hardware-timer sampling, NVS-backed thresholds + sensor calibration, deep sleep. **This is what's flashed on the XIAO right now.**
- **`firmware/platformio/src/main.cpp`** (PlatformIO) — an early scaffold returning dummy random data, with a different SSID (`XIAO-ESP32`) and password (`12345678`), plus ArduinoOTA. Not currently used.

Pick one before v0.2.

---

## Setup & Flashing (Arduino IDE)

### One-time setup

**Arduino IDE 2.3.8+.** Tools menu:
- Board: `ESP32 Arduino → ESP32C3 Dev Module` (or `XIAO_ESP32C3`)
- USB CDC On Boot: `Enabled`
- Partition Scheme: `Default 4MB with spiffs (1.2MB APP / 1.5MB SPIFFS)`
- Flash Size: `4MB (32Mb)`
- CPU Frequency: `160MHz`

**Library Manager** (`Tools → Manage Libraries…`):
- `ESPAsyncWebServer` by ESP32Async
- `AsyncTCP` by ESP32Async

(`WiFi`, `LittleFS`, `Preferences`, `Wire` are built into the ESP32 Arduino core.)

**LittleFS upload plugin:** download the `.vsix` from [arduino-littlefs-upload releases](https://github.com/earlephilhower/arduino-littlefs-upload/releases) and run:
```bash
mkdir -p ~/.arduinoIDE/plugins && mv ~/Downloads/arduino-littlefs-upload-*.vsix ~/.arduinoIDE/plugins/
```
Then quit and reopen Arduino IDE.

### Each upload

1. Open `firmware/SoleSense/SoleSense.ino` in Arduino IDE.
2. Click `→` (Upload). Wait for "Done uploading."
3. **Close Serial Monitor** (it holds the port).
4. `Cmd+Shift+P` → `Upload LittleFS to Pico/ESP8266/ESP32` → Enter.
5. Re-open Serial Monitor at 115200 baud, tap reset on the XIAO.

### Expected boot output

```
=== SoleSense booting ===
[FS] Mounted - <N> / 1441792 bytes used
[Sensors] mux + MPU-6050 initialised
[NVS] thresholds + calibration loaded
[WiFi] AP 'SoleSense' up at 192.168.4.1
[HTTP] server started
```

### Test the demo

Phone → connect to WiFi `SoleSense` (password `solesense`, no internet — expected) → open `http://192.168.4.1/` in Safari/Chrome.

---

## Roadmap

### v0.1 — *current, deployed*
- [x] Firmware skeleton, all 10 HTTP endpoints
- [x] 50 Hz hardware-timer sampling with 25-row ring-buffered CSV writes
- [x] NVS-backed thresholds + FSR/IMU calibration
- [x] Deep sleep + GPIO9 wake
- [x] Frontend SPA integrated and serving from LittleFS
- [x] End-to-end verified on hardware
- [ ] FSRs + mux soldered and reading real pressure
- [ ] IMU soldered and reading real motion

### v0.2 — *post-demo*
- [ ] Unify the two recording paths (currently frontend records client-side at 5 Hz; firmware backend records server-side at 50 Hz — pick one)
- [ ] Pick canonical firmware build system (Arduino IDE vs PlatformIO)
- [ ] Calibration UI in the frontend
- [ ] Inline Google Fonts as base64 (frontend currently falls back to system fonts on the AP because `fonts.googleapis.com` isn't reachable)
- [ ] Wire the 7 injury flags (currently only duration / sample count / L-R balance shown)
- [ ] Averaged 5 Hz writes with peak preservation (extends recording from ~5 min to ~40 min)

### v1.0 — *future*
- [ ] CNN/LSTM model trained on collected CSV data (per Choi et al. 2024)
- [ ] Real-time CoP trajectory visualization
- [ ] Cadence audio feedback via BLE
- [ ] Left/right insole pairing over ESP-NOW
- [ ] Mobile app wrapper

---

## Team

| Name | Role |
|---|---|
| Norton Hoang | Electrical & Firmware Lead |
| Maxim Varakuta | Electrical & Firmware |
| Jordan Chan | Mechanical Lead |
| Kiara Peters | Mechanical |
| James Kim | Mechanical |
| Daniel Grivennikov | Mechanical |
| Ethan Kim | Mechanical |
| Andony Velasquez | Software Lead |
| Dao Doan | Firmware / Software |
| Jasmine Dhaliwal | Software |
| Natalie Dai | Software |

---

## Documentation

- [`SOLESENSE.md`](SOLESENSE.md) — canonical project spec (hardware, firmware, frontend, data pipeline, injury flags, research basis)
- [`firmware/README.md`](firmware/README.md) — Arduino IDE vs PlatformIO firmware breakdown
- [`docs/superpowers/specs/2026-05-04-solesense-firmware-design.md`](docs/superpowers/specs/2026-05-04-solesense-firmware-design.md) — v0.1 firmware design doc
- [`docs/superpowers/plans/2026-05-04-solesense-firmware-v0.1.md`](docs/superpowers/plans/2026-05-04-solesense-firmware-v0.1.md) — v0.1 implementation plan
- [`docs/pseudocode/`](docs/pseudocode/) — Andony's system-level pseudocode (high-level flow + detailed injury analysis)
