# SoleSense

**3D Printed Smart Running Insole -- Biomechanical Analysis System**
*UCI . Spring 2026*

---

## Status

| Track | State |
|---|---|
| **Firmware** ([`firmware/SoleSenseV2/`](firmware/SoleSenseV2/)) | [x] **500 Hz sampling**, all metrics on-MCU: any-zone OR-gate step counter + GCT (with IMU sensor-fusion when wired), FSR-jerk loading rate using per-user body weight, 1024-sample Goertzel FFT, multi-slot crash-recoverable storage, pause-on-disconnect, NVS-backed user accounts with PIN auth, mDNS hostname. Open follow-ups: per-FSR saturation calibration, hardware verification under real running. See [`firmware/SoleSenseV2/README.md`](firmware/SoleSenseV2/README.md) for the full status table. |
| **Frontend** ([`software/frontend/solesense-v2/`](software/frontend/solesense-v2/)) | [x] Anatomical foot SVG, 3-zone x medial/lateral live readout, NVS-backed auth + self-signup, AI Coach via the Cloudflare Worker. All headline metrics display real numbers. |
| **Sensors** | 6 FSRs in 3-zone x medial/lateral layout (Choi 2024 +E-at-heel): 2 heel + 2 midfoot + 2 forefoot. Hardware bring-up + per-channel verification ongoing. IMU optional (not required for any of the headline metrics). |
| **Mechanical (TPU shell, PCB)** | In progress separately by the mechanical/electrical team. |

> **Architecture:** all run state lives on the MCU. The firmware computes the analysis on-device (FFT magnitudes + outliers + Welford stats in a CRC ring buffer); the browser is purely a renderer. WiFi blips no longer cost runs because the phone holds no state. Full rationale in [`docs/design/specs/2026-05-06-v0.2-data-architecture.md`](docs/design/specs/2026-05-06-v0.2-data-architecture.md).

---

## What SoleSense Does

A self-contained biomechanical analysis insole that records pressure and motion data during a run and serves a single-page web report over its own WiFi access point -- no internet, no app install, no cloud.

**Core loop:**
1. Power on -> ESP32-C3 boots, starts WiFi AP `SoleSense`
2. User connects phone to AP -> opens `http://solesense.local/` (or `http://192.168.4.1`)
3. Logs in / claims the device on first use -> Taps **Start Run** -> firmware samples sensors at 500 Hz
4. Taps **Stop** -> firmware finalises FFT magnitudes + Welford stats + step detector, frontend polls `/api/run-report` and renders
5. Report screen shows cadence, ground contact time, pronation, medial/lateral balance, pressure distribution by zone, impact rate (Healthy / Elevated / High), and up to 7 injury risk flags, optionally followed by a personalised AI Coach analysis

---

## Detected Injury Patterns

Sourced from peer-reviewed biomechanics literature (full citations in [`docs/research.md`](docs/research.md)):

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
| Seeed XIAO ESP32-C3 | MCU -- reads sensors, hosts WiFi AP, serves SPA | in hand, flashed |
| MPU-6050 | 3-axis accel + 3-axis gyro (I^2C) | in hand, soldering pending |
| FSR 402 x 6 | Pressure sensors (2 heel + 2 midfoot + 2 forefoot, medial/lateral pairs) | pending |
| LIR2450 x 2 in parallel | 3.7 V 120 mAh Li-ion coin cells (~240 mAh combined) | pending |
| TPU 85A filament | 3D-printed insole shell, gyroid 20-25% infill | pending |

---

## Pin Map

Per [`hardware/electricalpins.pdf`](hardware/electricalpins.pdf). XIAO ESP32-C3 Seeed pin name -> GPIO -> use:

```
XIAO ESP32-C3 -> MPU-6050 (I^2C):
  D4  (GPIO6)   -> SDA           (I2C data)
  D5  (GPIO7)   -> SCL           (I2C clock)
  D6  (GPIO21)  -> INT           (MPU-6050 INT, reserved; unused today)
  3V3           -> VCC
  GND           -> GND
  GND           -> AD0           (sets I2C address to 0x68)

XIAO -> 6 FSRs (no multiplexer; 2 sets of 3 with shared analog reads):
  Power lines (digital, one per set):
    D7  (GPIO20)  -> Set 1 power  (FSRs 1A, 1B, 1C)
    D8  (GPIO8)   -> Set 2 power  (FSRs 2A, 2B, 2C)
  Analog inputs (shared between sets):
    D0  (GPIO2)   -> ADC A        (reads FSR 1A or 2A, whichever set is powered)
    D1  (GPIO3)   -> ADC B        (reads FSR 1B or 2B)
    D2  (GPIO4)   -> ADC C        (reads FSR 1C or 2C)

Per-FSR wiring (each FSR identical):
    Pin 1 -> its set's digital power pin (Set 1's D7 / Set 2's D8)
    Pin 2 -> its set's analog input AND through a 10 kOhm pull-down to GND
            (standard FSR voltage divider -- the resistor is required)

FSR-to-zone mapping (3-zone x medial/lateral, Choi 2024 +E-at-heel):
    FSR 1A (Set 1, ADC A) -> Heel medial         (ch0)
    FSR 1B (Set 1, ADC B) -> Heel lateral        (ch1)
    FSR 1C (Set 1, ADC C) -> Midfoot medial      (ch2)
    FSR 2A (Set 2, ADC A) -> Midfoot lateral     (ch3)
    FSR 2B (Set 2, ADC B) -> Forefoot medial     (ch4)
    FSR 2C (Set 2, ADC C) -> Forefoot lateral    (ch5)

Read sequence (firmware does this automatically per sample):
    1. Drive PIN_PWR_SET1 HIGH, PIN_PWR_SET2 LOW -> read ADC A/B/C -> gFsr[0..2]
    2. Drive PIN_PWR_SET2 HIGH, PIN_PWR_SET1 LOW -> read ADC A/B/C -> gFsr[3..5]
    3. Both LOW between samples (active ground; not floating, to kill ghost crosstalk)

Wake button:
  D9 (GPIO9) -- uses the on-board BOOT button on the XIAO; no extra hardware
```

Pin defines live in [`firmware/SoleSenseV2/config.h`](firmware/SoleSenseV2/config.h).

---

## API

All endpoints served at `http://solesense.local/` (or `http://192.168.4.1`) once connected to the `SoleSense` WiFi AP.

| Method | Path | Auth | Purpose |
|---|---|---|---|
| `GET` | `/` | public | Serves the frontend SPA from LittleFS |
| `GET` | `/api/device` | public | Device info: firmware/version, board, sample rate, free heap, state, fs bytes |
| `GET` | `/api/sensor` | public | Live FSR + IMU snapshot; returns both raw `fsr[]` and EMA-smoothed `fsrEma[]` |
| `GET` | `/api/run-state` | public | Live run state: `recording, run_active, elapsed_ms, sample_count, clients_connected, outlier_count` |
| `GET` | `/api/run-report` | public | Computed metrics: steps, cadence, contactMs, loadingRate, pronate, medialPct, lateralPct, zoneAvg{heel,midfoot,forefoot}, imuConnected, imuImpacts, maxTotalPressure, flags |
| `GET` | `/api/run-spectrum` | public | FFT magnitude spectrum (12 channels x 18 bins, Goertzel) |
| `GET` | `/api/run-outliers` | public | Top-N outliers by sigma: `ts, channel, value, sigma` |
| `GET` | `/api/storage-state` | public | Slot count + per-slot byte usage in the run-snapshot ring buffer |
| `POST` | `/api/storage-selftest` | public | On-bench validation of multi-slot ring buffer |
| `POST` | `/api/fft-selftest` | public | Feeds a 1.95 Hz sine into channel 0; expect ~100 magnitude on the on-bin frequency |
| `GET` | `/api/auth/state` | public | `{ ownerExists, sessionActive, username, userCount, maxUsers }` |
| `POST` | `/api/auth/register` | public (capped) | Create account. URL-encoded `username, pin, body_kg`. Capped at `MAX_USERS = 20` and a 3-per-60-s rate limit. |
| `POST` | `/api/auth/login` | public (rate-limited) | URL-encoded `username, pin` -> `{ token, body_kg, username }` |
| `POST` | `/api/auth/logout` | session | Clear active session |
| `GET` | `/api/auth/profile` | session | Current user info |
| `POST` | `/api/start` | session | Begin recording |
| `POST` | `/api/stop` | session | End recording, flush state |
| `POST` | `/api/calibrate/zero` | session | Zero the 6 FSRs (insole unloaded), persist to NVS |
| `POST` | `/api/calibrate/imu` | session | Zero accel + gyro offsets (insole flat), persist to NVS |
| `POST` | `/api/sleep` | session | Enter deep sleep; wake on GPIO9 LOW |

Protected endpoints require `Authorization: Bearer <token>`. Full handler-by-handler detail is in [`firmware/SoleSenseV2/README.md`](firmware/SoleSenseV2/README.md).

---

## Repository Structure

```
solesense/
|-- README.md                         <- you are here
|-- LICENSE                           <- MIT
|-- .gitignore
|
|-- firmware/                         <- see firmware/README.md
|   |-- README.md
|   |-- SoleSenseV2/                  <- canonical firmware (Arduino IDE)
|   |   |-- SoleSenseV2.ino
|   |   |-- config.h / state.* / sensors.* / fft.* / outliers.* / stats.* / storage.* / auth.* / http_routes.*
|   |   |-- flash-littlefs.sh
|   |   `-- data/index.html           <- LittleFS deployment copy of solesense-v2/index.html
|   `-- imu-test/                     <- standalone MPU-6050 smoke-test sketch
|
|-- software/                         <- all browser/host-side code, see software/README.md
|   |-- README.md
|   |-- frontend/
|   |   |-- README.md
|   |   |-- mock-server.py            <- Python http.server simulating the firmware
|   |   `-- solesense-v2/index.html   <- canonical SPA (auth + AI Coach)
|   |-- backend/
|   |   `-- analyze-worker/           <- Cloudflare Worker proxy holding the OpenAI key
|   `-- scripts/                      <- host-side Python utilities (FSR + IMU bring-up)
|
|-- hardware/
|   |-- cad/                          <- SolidWorks CAD parts and assemblies
|   |-- electrical/Solesense_WD.*     <- KiCad schematic + PCB
|   |-- 3d printing/                  <- sliced .3mf gcode for the housing + lid
|   `-- electricalpins.pdf            <- canonical pin assignment reference
|
`-- docs/
    |-- research.md                   <- peer-reviewed sources backing the thresholds + sample rate
    `-- design/
        |-- specs/                    <- architecture + profile-system specs
        `-- plans/                    <- implementation plan
```

---

## Setup & Flashing

### One-time Arduino IDE setup

**Arduino IDE 2.3.8+.** Tools menu:
- Board: `ESP32 Arduino -> ESP32C3 Dev Module` (or `XIAO_ESP32C3`)
- USB CDC On Boot: `Enabled`
- Partition Scheme: `Default 4MB with spiffs (1.2MB APP / 1.5MB SPIFFS)`
- Flash Size: `4MB (32Mb)`
- CPU Frequency: `160MHz`

**Library Manager** (`Tools -> Manage Libraries...`):
- `ESPAsyncWebServer` by ESP32Async
- `AsyncTCP` by ESP32Async

(`WiFi`, `LittleFS`, `Preferences`, `Wire` are built into the ESP32 Arduino core.)

### Flashing the sketch

In Arduino IDE: open `firmware/SoleSenseV2/SoleSenseV2.ino` -> click Upload (`->`). From the terminal:

```bash
ARDUINO_CLI="/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"
"$ARDUINO_CLI" compile --fqbn esp32:esp32:XIAO_ESP32C3 firmware/SoleSenseV2
"$ARDUINO_CLI" upload  --fqbn esp32:esp32:XIAO_ESP32C3 --port /dev/cu.usbmodem2101 firmware/SoleSenseV2
```

### Flashing the LittleFS data (frontend)

```bash
cp software/frontend/solesense-v2/index.html firmware/SoleSenseV2/data/index.html
bash firmware/SoleSenseV2/flash-littlefs.sh
```

The script auto-detects `mklittlefs`, `esptool`, and the XIAO's USB port. Close Serial Monitor first -- it locks the port.

### Expected boot output

Open Serial Monitor at 115200 baud, tap reset on the XIAO:
```
=== SoleSense booting ===
[FS] Mounted - <N> / 1441792 bytes used
[Sensors] FSR sets + MPU-6050 initialised
[Storage] init: <N>/10 slot files present
[Auth] init; ownerExists=<yes|no>
[WiFi] AP 'SoleSense' up at 192.168.4.1
[mDNS] solesense.local resolving
[HTTP] server started
[Timer] 500 Hz sampling armed
```

### Test the demo

Phone -> connect to WiFi `SoleSense` (password `solesense`, no internet -- expected) -> open `http://192.168.4.1/` in Safari/Chrome.

If the page hangs on iPhone: turn off Wi-Fi Assist (`Settings -> Cellular`) so iOS doesn't silently fall back to cellular. Or force-quit Safari and retry.

---

## Roadmap

### Shipped
- [x] **All run state on the MCU. No browser-side storage.** FFT-coefficients + outlier-buffer in RAM, periodically flushed to a 10-slot ring buffer in flash. Run timer derived from the latest valid flash slot -- not a JS wall-clock -- so disconnects pause the duration counter and reconnects resume from the last persisted state. See [`docs/design/specs/2026-05-06-v0.2-data-architecture.md`](docs/design/specs/2026-05-06-v0.2-data-architecture.md) for the full design.
- [x] **500 Hz sampling.** RAM usage is rate-independent because Goertzel is incremental. Captures impact rising edges with enough resolution for FSR-jerk extrapolation.
- [x] **Time-domain step counter and ground-contact-time** (any-zone OR-gate, 250 ms refractory, IMU sensor-fusion when wired).
- [x] **FSR-jerk loading rate (BW/s)** -- peak heel d(ADC)/dt converted via FSR-saturation x body-weight assumptions.
- [x] **3-zone x medial/lateral sensor layout** (Choi 2024 +E-at-heel): 2 heel + 2 midfoot + 2 forefoot.
- [x] **Anatomical foot diagram** in the frontend: cut-out CAD render of the actual insole with live-data overlays on the six visible sensor pads.
- [x] **NVS-backed user accounts with PIN auth + self-signup**, mDNS hostname, captive-portal-free flow.
- [x] **AI Coach panel** via Cloudflare Worker (`software/backend/analyze-worker/`).

### Open follow-ups
- [ ] Per-FSR saturation calibration so the loading-rate conversion stops assuming `ADC=4095 ↔ 10 kg of force` for every channel. (Body weight is already per-user via the auth profile; 70 kg is only the no-session fallback.)
- [ ] EMA-baseline tracking in the step detector for FSR baseline drift (sweat / temperature).
- [ ] End-to-end hardware verification: 30 s real-run test on a fully-wired insole.
- [ ] Pick canonical firmware build system (Arduino IDE vs PlatformIO).
- [ ] Inline Google Fonts as base64 (any UI that uses them fails on the AP because no internet).

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

- [`firmware/README.md`](firmware/README.md) -- firmware overview + flash instructions
- [`firmware/SoleSenseV2/README.md`](firmware/SoleSenseV2/README.md) -- module status, security model, known caveats
- [`software/README.md`](software/README.md) and [`software/frontend/README.md`](software/frontend/README.md) -- frontend layout, mock-server usage, UI swap procedure
- [`software/backend/analyze-worker/README.md`](software/backend/analyze-worker/README.md) -- Cloudflare Worker setup for the AI Coach
- [`docs/research.md`](docs/research.md) -- peer-reviewed sources backing the thresholds + sample rate
- [`docs/design/specs/2026-05-06-v0.2-data-architecture.md`](docs/design/specs/2026-05-06-v0.2-data-architecture.md) -- data architecture (FFT + outliers, MCU as source of truth, no browser-side state)
- [`docs/design/specs/2026-05-09-profile-system.md`](docs/design/specs/2026-05-09-profile-system.md) -- profile / auth system spec
- [`docs/design/plans/2026-05-06-v0.2-firmware.md`](docs/design/plans/2026-05-06-v0.2-firmware.md) -- firmware implementation plan
