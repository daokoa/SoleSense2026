# SoleSense

[![Firmware compile](https://github.com/daokoa/SoleSense2026/actions/workflows/firmware-compile.yml/badge.svg)](https://github.com/daokoa/SoleSense2026/actions/workflows/firmware-compile.yml)
[![Firmware](https://img.shields.io/badge/firmware-Arduino%20ESP32--C3-blue)](firmware/SoleSenseV2/)
[![Frontend](https://img.shields.io/badge/frontend-single--file%20SPA-green)](software/frontend/solesense-v2/)
[![Backend](https://img.shields.io/badge/backend-Cloudflare%20Worker-f38020)](software/backend/analyze-worker/)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue)](LICENSE)

A 3D-printed smart running insole that delivers full biomechanical analysis offline — cadence, ground contact time, impact rate, pronation, foot-pressure heatmap, and research-backed injury flags — over the device's own WiFi access point. No app, no cloud, no internet.

*UCI · Spring 2026*

---

## Quickstart

1. **Power on** the insole.
2. **Connect** your phone to WiFi `SoleSense` (password `solesense`).
3. **Open** `http://192.168.4.1/` (or `http://solesense.local/` on iOS / macOS).
4. **Sign in**, tap **Start Run**, run, tap **Stop**, read the report.

---

## Features

**Firmware** — ESP32-C3 @ 500 Hz
- Six-FSR matrix-scan + MPU-6050 IMU. All analysis on-device.
- Incremental Goertzel FFT, top-N outlier buffer, Welford running stats — no raw sample storage.
- Multi-slot crash-recoverable run snapshots in flash; runs survive WiFi blips and phone disconnects.
- NVS-backed user accounts with SHA-256 + per-user salt PIN hashing. One in-RAM session at a time.

**Frontend** — single self-contained HTML file
- Sign-in / device-claim flow, live timer, anatomical foot diagram with real-time FSR fill.
- Report screen with verdict card, foot-pressure heatmap, findings cards, and optional AI Coach (proxied through a Cloudflare Worker that holds the OpenAI key — the device never touches it).

**Injury flags** — research-backed thresholds (full citations in [`docs/research.md`](docs/research.md))
- Heel striking · high loading rate · low cadence · overpronation · supination · medial/lateral asymmetry · long ground contact time

---

## How it works

The MCU samples 6 FSRs + 6 IMU axes at 500 Hz, runs the full analysis on-device (FFT magnitudes + Welford stats + outlier buffer flushed every 3 s to a 10-slot ring buffer in flash). The phone is a thin renderer: it polls `/api/run-state` for the timer and `/api/run-report` for the metrics. Disconnects pause the run cleanly; reconnects resume from the latest valid flash slot.

Full architecture rationale: [`docs/design/specs/2026-05-06-v0.2-data-architecture.md`](docs/design/specs/2026-05-06-v0.2-data-architecture.md).

---

## Hardware

| Component | Role | Status |
|---|---|---|
| Seeed XIAO ESP32-C3 | MCU, WiFi AP, HTTP server | flashed, running |
| MPU-6050 | 6-axis accel + gyro (I²C @ `0x68`) | wired, reads validated |
| FSR 402 × 6 | Pressure sensors (3-zone × medial/lateral) | wiring in progress |
| LIR2450 × 2 (parallel) | 3.7 V Li-ion coin cells, ~240 mAh | pending |
| TPU 85A | 3D-printed insole shell | pending |

---

## Pin map

Per [`hardware/electricalpins.pdf`](hardware/electricalpins.pdf); pin constants in [`firmware/SoleSenseV2/config.h`](firmware/SoleSenseV2/config.h).

| XIAO pin | GPIO | Use |
|---|---|---|
| D0 / A0 | 2 | ADC A — shared by FSR 1A & 2A |
| D1 / A1 | 3 | ADC B — shared by FSR 1B & 2B |
| D2 / A2 | 4 | ADC C — shared by FSR 1C & 2C |
| D4 | 6 | I²C SDA → MPU-6050 |
| D5 | 7 | I²C SCL → MPU-6050 |
| D6 | 21 | MPU-6050 INT (reserved) |
| D7 | 20 | FSR Set 1 power (FSRs 1A, 1B, 1C) |
| D8 | 8 | FSR Set 2 power (FSRs 2A, 2B, 2C) |
| D9 | 9 | Wake button (on-board BOOT) |

### FSR wiring

**Three pull-down resistors total**, one per ADC pin — *not* one per FSR. Each pull-down forms the bottom half of a voltage divider with whichever of the two FSRs on that ADC is currently powered.

```
D7 ──[FSR 1A]──┐
                ├── D0 ──[10 kΩ]── GND
D8 ──[FSR 2A]──┘
```

The firmware time-multiplexes which set is active. Only one FSR is ever in the divider at a time; the inactive set's power pin is driven LOW so any ghost-current path through the unpowered FSR shorts to ground rather than perturbing the active read.

### FSR → zone mapping (Choi 2024, +E-at-heel)

| Channel | FSR | Zone |
|---|---|---|
| ch0 | 1A | Heel medial |
| ch1 | 1B | Heel lateral |
| ch2 | 1C | Midfoot medial |
| ch3 | 2A | Midfoot lateral |
| ch4 | 2B | Forefoot medial |
| ch5 | 2C | Forefoot lateral |

---

## API

| Method | Path | Auth | Purpose |
|---|---|---|---|
| `GET`  | `/`                      | public  | Serves the SPA from LittleFS |
| `GET`  | `/api/device`            | public  | Firmware version, board, sample rate, free heap, fs bytes |
| `GET`  | `/api/sensor`            | public  | Live FSR + IMU snapshot (`fsr[]`, `fsrEma[]`, `ax/ay/az`, `gx/gy/gz`) |
| `GET`  | `/api/run-state`         | public  | `recording, run_active, elapsed_ms, sample_count, clients_connected, outlier_count` |
| `GET`  | `/api/run-report`        | public  | Final metrics: steps, cadence, contactMs, loadingRate, pronate, medialPct, lateralPct, zoneAvg, imuConnected, imuImpacts, flags |
| `GET`  | `/api/run-spectrum`      | public  | FFT magnitude (12 channels × 18 bins, Goertzel) |
| `GET`  | `/api/run-outliers`      | public  | Top-N outliers (`ts, channel, value, sigma`) |
| `GET`  | `/api/storage-state`     | public  | Slot count + per-slot byte usage |
| `POST` | `/api/storage-selftest`  | public  | On-bench validation of the ring buffer |
| `POST` | `/api/fft-selftest`      | public  | 1.95 Hz sine validation — expect ~100 mag on the on-bin frequency |
| `GET`  | `/api/auth/state`        | public  | `ownerExists, sessionActive, username, userCount, maxUsers` |
| `POST` | `/api/auth/register`     | public, capped (`MAX_USERS=20`, 3-per-60-s) | Walk-up signup; first call claims the device |
| `POST` | `/api/auth/login`        | rate-limited | `username, pin → token, body_kg` |
| `POST` | `/api/auth/logout`       | session | Clear active session |
| `GET`  | `/api/auth/profile`      | session | Current user info |
| `POST` | `/api/start`             | session | Begin recording |
| `POST` | `/api/stop`              | session | End recording, flush state |
| `POST` | `/api/calibrate/zero`    | session | Zero the 6 FSRs (insole unloaded) |
| `POST` | `/api/calibrate/imu`     | session | Zero accel + gyro (insole flat) |
| `POST` | `/api/sleep`             | session | Enter deep sleep; wake on GPIO9 LOW |

Protected endpoints require `Authorization: Bearer <token>`. Handler-by-handler detail in [`firmware/SoleSenseV2/README.md`](firmware/SoleSenseV2/README.md).

---

## Setup & build

### One-time Arduino IDE setup

**Arduino IDE 2.3.8+** — Tools menu:
- Board: `XIAO_ESP32C3`
- Partition Scheme: `Default 4MB with spiffs (1.2MB APP / 1.5MB SPIFFS)`
- USB CDC On Boot: `Enabled`

**Libraries** (Library Manager): `ESPAsyncWebServer` and `AsyncTCP`, both by **ESP32Async**. (`WiFi`, `LittleFS`, `Preferences`, `Wire` ship with the ESP32 core.)

### Flash from terminal

```bash
ARDUINO_CLI="/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"

# Firmware
"$ARDUINO_CLI" compile --fqbn esp32:esp32:XIAO_ESP32C3 firmware/SoleSenseV2
"$ARDUINO_CLI" upload  --fqbn esp32:esp32:XIAO_ESP32C3 --port /dev/cu.usbmodem2101 firmware/SoleSenseV2

# Frontend (LittleFS partition)
cp software/frontend/solesense-v2/index.html firmware/SoleSenseV2/data/index.html
bash firmware/SoleSenseV2/flash-littlefs.sh
```

Close any open Serial Monitor before flashing — Arduino IDE auto-spawns a `serial-mo` process that holds the port.

### Expected boot output

```
=== SoleSense v0.2 booting ===
[FS] Mounted - <N> / 1441792 bytes used
[Sensors] FSR sets + MPU-6050 initialised
[Storage] init: <N>/10 slot files present
[Auth] init; ownerExists=<yes|no>
[WiFi] AP 'SoleSense' up at 192.168.4.1
[mDNS] solesense.local resolving
[HTTP] server started
[Timer] 500 Hz sampling armed
```

---

## Repository layout

```
solesense/
├── README.md                         · this file
├── LICENSE                           · MIT
│
├── firmware/
│   ├── README.md
│   ├── SoleSenseV2/                  · canonical firmware (Arduino)
│   │   ├── SoleSenseV2.ino
│   │   ├── config.h, state.*, sensors.*, fft.*, outliers.*, stats.*, storage.*, auth.*, http_routes.*
│   │   ├── flash-littlefs.sh
│   │   └── data/index.html           · LittleFS copy of the SPA
│   └── imu-test/                     · standalone MPU-6050 smoke-test sketch
│
├── software/
│   ├── README.md
│   ├── frontend/
│   │   ├── README.md
│   │   ├── mock-server.py            · Python http.server simulating the firmware
│   │   └── solesense-v2/index.html   · canonical SPA
│   ├── backend/
│   │   └── analyze-worker/           · Cloudflare Worker (holds the OpenAI key)
│   └── scripts/                      · host-side Python tooling (FSR + IMU bring-up)
│
├── hardware/
│   ├── cad/                          · SolidWorks parts + assemblies
│   ├── electrical/Solesense_WD.*     · KiCad schematic + PCB
│   ├── 3d printing/                  · sliced .3mf gcode (housing + lid)
│   └── electricalpins.pdf            · canonical pin assignment reference
│
└── docs/
    ├── research.md                   · peer-reviewed sources
    └── design/
        ├── specs/                    · architecture + profile-system specs
        └── plans/                    · firmware implementation plan
```

---

## Roadmap

### Shipped
- All run state on the MCU. Run timer derived from the latest valid flash slot, so disconnects pause cleanly and reconnects resume from persisted state.
- 500 Hz sampling with incremental Goertzel FFT; RAM usage is rate-independent.
- Time-domain step counter + ground contact time (any-zone OR-gate, 250 ms refractory, IMU sensor-fusion when wired).
- FSR-jerk loading rate (BW/s), converted using the logged-in user's body weight.
- Three-zone × medial/lateral sensor layout (Choi 2024, +E-at-heel).
- Anatomical foot diagram with live FSR fill on the recording screen and a pressure heatmap on the report.
- NVS-backed user accounts (PIN auth + walk-up self-signup), mDNS hostname, captive-portal-free flow.
- AI Coach via Cloudflare Worker — keeps the OpenAI key off the device.

### Open follow-ups
- Per-FSR saturation calibration (the loading-rate conversion currently assumes `ADC = 4095 ↔ 10 kg of force` uniformly).
- EMA-baseline tracking in the step detector for FSR baseline drift (sweat / temperature).
- 30-second real-run validation on a fully-wired insole.
- Pick a canonical firmware build system (Arduino IDE vs PlatformIO).
- Inline Google Fonts as base64 so the AI-Coach offline fallback survives even when the UI uses webfonts.

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

- [`firmware/README.md`](firmware/README.md) — firmware overview + flash instructions
- [`firmware/SoleSenseV2/README.md`](firmware/SoleSenseV2/README.md) — module status, security model, caveats
- [`software/README.md`](software/README.md) · [`software/frontend/README.md`](software/frontend/README.md) — frontend + mock-server
- [`software/backend/analyze-worker/README.md`](software/backend/analyze-worker/README.md) — Cloudflare Worker (AI Coach)
- [`docs/research.md`](docs/research.md) — peer-reviewed sources backing the thresholds + sample rate
- [`docs/design/specs/2026-05-06-v0.2-data-architecture.md`](docs/design/specs/2026-05-06-v0.2-data-architecture.md) — data architecture
- [`docs/design/specs/2026-05-09-profile-system.md`](docs/design/specs/2026-05-09-profile-system.md) — auth / profile system
- [`docs/design/plans/2026-05-06-v0.2-firmware.md`](docs/design/plans/2026-05-06-v0.2-firmware.md) — firmware implementation plan

---

## License

MIT — see [`LICENSE`](LICENSE).
