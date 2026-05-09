# SoleSense

**3D Printed Smart Running Insole — Biomechanical Analysis System**
*SoleSense team · UCI · Spring 2026*

---

## Status

| Track | State |
|---|---|
| **Firmware v0.1** (demo) | ✅ Flashed and running. Records 50 Hz CSV to LittleFS, browser-side JS analysis. This is what's on the device for live demos. |
| **Firmware v0.2** (active development) | ✅ Modular rewrite, **500 Hz sampling**, all metrics on-MCU: time-domain step counter + GCT, FSR-jerk loading rate (BW/s), 1024-sample Goertzel FFT, multi-slot crash-recoverable storage, pause-on-disconnect. Open follow-ups: per-user body-weight + FSR-saturation calibration for loading rate. See [`firmware/SoleSenseV2/README.md`](firmware/SoleSenseV2/README.md) for the full status table. |
| **Frontend dao** (v0.1-compat) | ✅ White/blue UI with foot-diagram recording screen, JS-side analysis pipeline. |
| **Frontend dao-v2** (v0.2-compat) | ✅ Anatomical foot SVG, 3-zone × medial/lateral live readout, polls `/api/run-state` + `/api/run-report` instead of running JS analysis. All headline metrics now display real numbers. |
| **Frontend andony** | ✅ Dark-themed alternative SPA. Polls `/api/sensor` for live readout. |
| **Sensors** | 6 FSRs in 3-zone × medial/lateral layout (Choi 2024 +E-at-heel): 2 heel + 2 midfoot + 2 forefoot. Hardware bring-up + per-channel verification ongoing. IMU optional (not required for any of the headline metrics). |
| **Mechanical (TPU shell, PCB)** | In progress separately by the mechanical/electrical team. |

See **[Roadmap](#roadmap)** for the path to v1.0.

---

## What SoleSense Does

A self-contained biomechanical analysis insole that records pressure and motion data during a run and serves a single-page web report over its own WiFi access point — no internet, no app install, no cloud.

**Core loop:**
1. Power on → ESP32-C3 boots, starts WiFi AP `SoleSense`
2. User connects phone to AP → opens `http://192.168.4.1`
3. Taps **Start Run** → firmware samples sensors (50 Hz on v0.1, 500 Hz on v0.2)
4. Taps **Stop** → report is computed
   - **v0.1:** firmware writes raw CSV to LittleFS; frontend fetches and runs the JS analysis pipeline.
   - **v0.2:** firmware computes everything on-MCU (FFT + outlier buffer + Welford stats + time-domain step detector); frontend just polls `/api/run-report` and renders.
5. Report screen shows cadence, ground contact time, pronation, medial/lateral balance, pressure distribution by zone, loading rate (BW/s), and up to 7 injury risk flags

---

## Detected Injury Patterns

Sourced from peer-reviewed biomechanics literature (full citations in [`SOLESENSE.md`](SOLESENSE.md) §13):

- Heel striking
- High loading rate
- Low cadence
- Overpronation
- Supination
- Medial / lateral asymmetry (single-insole; renamed from "bilateral" since we have one foot's worth of sensors)
- Long ground contact time

Thresholds are baked in from research; not user-tunable in the UI.

---

## Hardware

| Component | Role | Status |
|---|---|---|
| Seeed XIAO ESP32-C3 | MCU — reads sensors, hosts WiFi AP, serves SPA | in hand, flashed |
| MPU-6050 | 3-axis accel + 3-axis gyro (I²C) | in hand, soldering pending |
| FSR 402 × 6 | Pressure sensors (heel, lateral/medial mid, lateral/medial ball, toe 1) | pending |
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

XIAO → 6 FSRs (no multiplexer — 2 sets of 3 with shared analog reads):
  Power lines (digital, one per set):
    GPIO5  (D3)  → Set 1 power (FSRs 1A, 1B, 1C)
    GPIO10 (D10) → Set 2 power (FSRs 2A, 2B, 2C)
  Analog inputs (shared between sets):
    GPIO2  (A0)  → ADC A — reads FSR 1A or 2A (whichever set is powered)
    GPIO3  (D1)  → ADC B — reads FSR 1B or 2B
    GPIO4  (D2)  → ADC C — reads FSR 1C or 2C

Per-FSR wiring (each FSR identical):
    Pin 1 → its set's digital power pin (Set 1's GPIO5 or Set 2's GPIO10)
    Pin 2 → its set's analog input AND through a 10kΩ pull-down to GND
            (standard FSR voltage divider — the resistor is required)

FSR-to-zone mapping:
    FSR 1A (Set 1, ADC A) → Heel
    FSR 1B (Set 1, ADC B) → Lateral Mid
    FSR 1C (Set 1, ADC C) → Medial Mid
    FSR 2A (Set 2, ADC A) → Ball Lateral
    FSR 2B (Set 2, ADC B) → Ball Medial
    FSR 2C (Set 2, ADC C) → Toe 1 (hallux)

Read sequence (firmware does this automatically per sample):
    1. Drive PIN_PWR_SET1 HIGH (Set 2 high-Z) → read ADC A/B/C → gFsr[0..2]
    2. Drive PIN_PWR_SET2 HIGH (Set 1 high-Z) → read ADC A/B/C → gFsr[3..5]
    3. Both high-Z between samples

Wake button:
  GPIO9 — uses the on-board BOOT button on the XIAO; no extra hardware
```

Pin defines live at the top of [`firmware/SoleSense/SoleSense.ino`](firmware/SoleSense/SoleSense.ino).

---

## API

All endpoints served at `http://192.168.4.1` once connected to the `SoleSense` WiFi AP.

### Endpoints in both v0.1 and v0.2

| Method | Path | Purpose |
|---|---|---|
| `GET` | `/` | Serves the frontend SPA from LittleFS |
| `GET` | `/api/device` | Device info: firmware/version, board, sample rate (50 on v0.1, 500 on v0.2), free heap, state, fs bytes, hasData, thresholds |
| `GET` | `/api/sensor` | Live FSR + IMU snapshot |
| `POST` | `/api/start` | Begin recording |
| `POST` | `/api/stop` | End recording, flush state |
| `POST` | `/api/data/clear` | Delete recorded run data |
| `POST` | `/api/calibrate/zero` | Zero the 6 FSRs (insole unloaded), persist to NVS |
| `POST` | `/api/calibrate/imu` | Zero accel + gyro offsets (insole flat), persist to NVS |
| `POST` | `/api/settings` | Update injury-flag thresholds (validated, persisted; not currently exposed in UI) |
| `POST` | `/api/sleep` | Enter deep sleep; wake on GPIO9 LOW |

### v0.1-only

| Method | Path | Purpose |
|---|---|---|
| `GET` | `/data.csv` | Stream the recorded 50 Hz CSV (409 while recording). Frontend parses + analyses in JS. |

CSV schema (13 columns, 50 Hz):
```
timestamp_ms, fsr1..fsr6, accel_x, accel_y, accel_z, gyro_x, gyro_y, gyro_z
```

### v0.2-only

| Method | Path | Purpose |
|---|---|---|
| `GET` | `/api/run-state` | Live run state: elapsed_ms (sourced from latest valid flash slot), sample count, paused flag, current state |
| `GET` | `/api/run-report` | Computed metrics: steps, cadence, contactMs, loadingRate (BW/s), pronate, medialPct, lateralPct, zoneAvg{heel,midfoot,forefoot}, flags |
| `GET` | `/api/run-spectrum` | FFT magnitude spectrum (12 channels × 18 bins, Goertzel) |
| `GET` | `/api/run-outliers` | Top-N outliers by σ (channel, ts_ms, value, delta, sigma) |
| `GET` | `/api/storage-selftest` | On-bench validation of multi-slot ring buffer (writes 3 slots, corrupts newest, asserts load_latest returns second-newest) |
| `GET` | `/api/fft-selftest` | Feeds a 1.95 Hz sine into channel 0 and prints per-bin magnitudes to Serial — expect ~100 amp on the on-bin frequency |

---

## Repository Structure

```
solesense/
├── README.md                         ← you are here
├── SOLESENSE.md                      ← canonical project spec
├── .gitignore
│
├── firmware/                         ← BOTH firmware paths grouped, see firmware/README.md
│   ├── README.md
│   ├── SoleSense/                    ← (Arduino IDE) WORKING firmware
│   │   ├── SoleSense.ino             ← v0.1 firmware, ~520 lines, flashed and verified
│   │   ├── flash-littlefs.sh         ← terminal-based LittleFS flash script
│   │   └── data/
│   │       └── index.html            ← LittleFS deployment copy of software/frontend/<ui>/index.html
│   └── platformio/                   ← (PlatformIO) parallel stub firmware
│       ├── platformio.ini
│       ├── src/main.cpp
│       ├── include/, lib/, test/
│
├── software/                         ← all browser/host-side code, see software/README.md
│   ├── README.md
│   └── frontend/                     ← two parallel UIs, see frontend/README.md
│       ├── README.md
│       ├── mock-server.py            ← Python http.server simulating the firmware
│       ├── dao/index.html            ← Dao's white/blue UI (currently active on device)
│       └── andony/index.html         ← Andony's dark SPA
│
├── hardware/
│   ├── cad/FSR Cutout.SLDPRT         ← SolidWorks CAD
│   ├── electrical/Solesense_WD.*     ← KiCad schematic + PCB
│   └── pcb/                          ← reserved for production PCB files (empty)
│
├── docs/
│   ├── pseudocode/                   ← system-level pseudocode (system-flow + injury-analysis)
│   │   ├── README.md
│   │   ├── system-flow.md
│   │   └── injury-analysis.md
│   └── superpowers/
│       ├── specs/                    ← v0.1 firmware design doc
│       └── plans/                    ← v0.1 implementation plan
│
└── assets/                           ← images, diagrams (reserved, empty)
```

### Note on the two firmwares

Both live under [`firmware/`](firmware/) — see [`firmware/README.md`](firmware/README.md) for the breakdown:

- **`firmware/SoleSense/SoleSense.ino`** (Arduino IDE) — the working v0.1 firmware. ~520 lines. All 10 endpoints, 50 Hz hardware-timer sampling, NVS-backed thresholds + sensor calibration, deep sleep. **This is what's flashed on the XIAO right now.**
- **`firmware/platformio/src/main.cpp`** (PlatformIO) — early scaffold returning dummy random data, with a different SSID (`XIAO-ESP32`) and password (`12345678`), plus ArduinoOTA. Not currently used.

Pick one before v0.2.

---

## Setup & Flashing

### One-time Arduino IDE setup

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

### Flashing the sketch

In Arduino IDE: open `firmware/SoleSense/SoleSense.ino` → click Upload (`→`).

### Flashing the LittleFS data (frontend)

Two paths — pick whichever works on your machine.

**Terminal (recommended, more reliable):**
```bash
# Pick which UI you want flashed:
cp software/frontend/dao/index.html firmware/SoleSense/data/index.html
# or:
cp software/frontend/andony/index.html firmware/SoleSense/data/index.html

# Then build + flash:
bash firmware/SoleSense/flash-littlefs.sh
```

The script auto-detects `mklittlefs`, `esptool`, and the XIAO's USB port. Close Serial Monitor first — it locks the port.

**Arduino IDE plugin:**
1. Sync your chosen UI as above
2. Close Serial Monitor
3. `Cmd+Shift+P` → `Upload LittleFS to Pico/ESP8266/ESP32` → Enter

(The plugin needs to be installed first — see `firmware/README.md`.)

### Expected boot output

Open Serial Monitor at 115200 baud, tap reset on the XIAO:
```
=== SoleSense booting ===
[FS] Mounted - <N> / 1441792 bytes used
[Sensors] FSR sets + MPU-6050 initialised
[NVS] thresholds + calibration loaded
[WiFi] AP 'SoleSense' up at 192.168.4.1
[HTTP] server started
```

### Test the demo

Phone → connect to WiFi `SoleSense` (password `solesense`, no internet — expected) → open `http://192.168.4.1/` in Safari/Chrome.

If the page hangs on iPhone: turn off Wi-Fi Assist (`Settings → Cellular`) so iOS doesn't silently fall back to cellular. Or force-quit Safari and retry.

---

## Roadmap

### v0.1 — *current, deployed*
- [x] Firmware skeleton, all 10 HTTP endpoints
- [x] 50 Hz hardware-timer sampling with 25-row ring-buffered CSV writes
- [x] NVS-backed thresholds + FSR/IMU calibration
- [x] Deep sleep + GPIO9 wake
- [x] Frontend SPA (Dao's UI) with home / recording / report / settings screens
- [x] Injury-flag analysis pipeline (7 flags) with research-based thresholds
- [x] Pressure-distribution-by-zone display (% of total foot load)
- [x] End-to-end verified on hardware
- [ ] FSRs soldered with 10 kΩ pull-downs and reading real pressure
- [ ] MPU-6050 soldered and reading real motion

### v0.2 — *current development*
- [x] **All run state on the MCU. No browser-side storage.** Raw-CSV recording replaced with FFT-coefficients + outlier-buffer in RAM, periodically flushed to a 10-slot ring buffer in flash. Run timer derived from the latest valid flash slot — not a JS wall-clock — so disconnects pause the duration counter and reconnects resume from the last persisted state. See [`docs/superpowers/specs/2026-05-06-v0.2-data-architecture.md`](docs/superpowers/specs/2026-05-06-v0.2-data-architecture.md) for the full design.
- [x] **Sample rate bumped to 500 Hz** (from 50 Hz). RAM usage is rate-independent because Goertzel is incremental. Captures impact rising edges with enough resolution for FSR-jerk extrapolation.
- [x] **Time-domain step counter and ground-contact-time** (Schmitt trigger on the heel composite, 150 ms refractory).
- [x] **FSR-jerk loading rate (BW/s)** — peak heel d(ADC)/dt converted via FSR-saturation × body-weight assumptions.
- [x] **3-zone × medial/lateral sensor layout** (Choi 2024 +E-at-heel): 2 heel + 2 midfoot + 2 forefoot.
- [x] **Anatomical foot SVG** in dao-v2: asymmetric medial/lateral edges, arch indent, toes anchored as ellipses.
- [ ] User-configurable body weight (currently hardcoded 70 kg) and per-FSR saturation calibration via `/api/settings`.
- [ ] EMA-baseline tracking in the step detector for FSR baseline drift (sweat / temperature).
- [ ] End-to-end hardware verification: 30 s real-run test on a fully-wired insole.
- [ ] Pick canonical firmware build system (Arduino IDE vs PlatformIO).
- [ ] Pick canonical frontend (dao-v2 or andony — only one survives).
- [ ] Inline Google Fonts as base64 (any UI that uses them fails on the AP because no internet).

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
- [`firmware/README.md`](firmware/README.md) — Arduino IDE vs PlatformIO firmware breakdown + flash instructions
- [`software/README.md`](software/README.md) and [`software/frontend/README.md`](software/frontend/README.md) — frontend layout, mock-server usage, UI swap procedure
- [`docs/superpowers/specs/2026-05-04-solesense-firmware-design.md`](docs/superpowers/specs/2026-05-04-solesense-firmware-design.md) — v0.1 firmware design doc
- [`docs/superpowers/specs/2026-05-06-v0.2-data-architecture.md`](docs/superpowers/specs/2026-05-06-v0.2-data-architecture.md) — v0.2 data architecture (FFT + outliers, MCU as source of truth, no browser-side state)
- [`docs/superpowers/plans/2026-05-04-solesense-firmware-v0.1.md`](docs/superpowers/plans/2026-05-04-solesense-firmware-v0.1.md) — v0.1 implementation plan
- [`docs/pseudocode/`](docs/pseudocode/) — system-level pseudocode (high-level flow + detailed injury analysis)
