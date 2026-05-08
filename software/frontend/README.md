# SoleSense Frontend

Three parallel UIs for the SoleSense web app, kept side by side. They all connect to the firmware HTTP API; visual design, target firmware version, and feature scope differ.

```
software/frontend/
├── README.md           ← this file
├── mock-server.py      ← Python http.server that simulates the firmware backend
├── dao/
│   └── index.html      ← Dao's UI for v0.1 firmware: parses /data.csv, runs analysis in JS
├── dao-v2/
│   └── index.html      ← Dao's UI rewired for v0.2 firmware: thin viewer, no JS-side analysis
└── andony/
    └── index.html      ← Andony's UI: dark theme with animated orbs, live-poll demo
```

**Which one matches which firmware:**

| Frontend | Firmware | Notes |
|---|---|---|
| `dao/` | `firmware/SoleSense/` (v0.1) | what's flashed for the demo. Analysis runs in browser from `/data.csv`. |
| `dao-v2/` | `firmware/SoleSenseV2/` (v0.2) | thin viewer. Polls `/api/run-state` for the timer, fetches `/api/run-report` for the analysis. Shows a "Paused" badge when the phone disconnects. |
| `andony/` | works against either | live-poll demo style. |

Both are self-contained single files. Andony's pulls Google Fonts via `@import`; Dao's uses system fonts. Each is served by the XIAO ESP32-C3 over its `SoleSense` WiFi AP at `http://192.168.4.1` — but only one at a time, since LittleFS only stores one `index.html`.

## Which UI is currently flashed

The active UI lives at `firmware/SoleSense/data/index.html` (the Arduino LittleFS plugin uploads from there). **Right now it's a copy of `dao/index.html`.**

## Switch the active UI

Sync the chosen UI into the firmware data folder, then re-flash LittleFS:

```bash
# Use Dao's UI:
cp software/frontend/dao/index.html firmware/SoleSense/data/index.html

# Use Andony's UI:
cp software/frontend/andony/index.html firmware/SoleSense/data/index.html

# Then flash:
bash firmware/SoleSense/flash-littlefs.sh
```

(See [`firmware/README.md`](../../firmware/README.md) for the flash script options.)

## Preview a UI locally without flashing

`mock-server.py` simulates the firmware backend with synthetic stride-shaped sensor data so you can preview either UI in any browser:

```bash
# From the repo root:
python3 software/frontend/mock-server.py            # serves dao/  (default)
python3 software/frontend/mock-server.py andony     # serves andony/
```

Then open <http://localhost:8080/>. The mock implements the same HTTP API as the real firmware (per `SOLESENSE.md` §10), so the UI behaves identically to how it would on the device. Ctrl+C to stop.

## Edit each UI

Edit the source file in `dao/` or `andony/` directly. Don't edit `firmware/SoleSense/data/index.html` — it's a deployment artifact that gets overwritten via the `cp` step above.

## What each UI does

| Feature | `dao/` | `andony/` |
|---|---|---|
| Home, Recording, Report, Settings screens | ✓ | ✓ (no Settings) |
| `POST /api/start` / `/api/stop` (server-side recording) | ✓ | (records client-side) |
| `GET /data.csv` (download recorded run from device flash) | ✓ | — |
| Live FSR + IMU readout during recording | — | ✓ (5 Hz poll of `/api/sensor`) |
| Injury-flag analysis (7 flags) | ✓ | ✓ |
| `POST /api/calibrate/zero` & `/api/calibrate/imu` UI buttons | ✓ | — |
| `POST /api/data/clear` UI button | ✓ | — |
| Pressure distribution by zone (% of total foot load) | ✓ | — |

The full HTTP API surface is documented in the root [`README.md`](../../README.md#api).

## Pressure Distribution by Zone (Dao's report)

Dao's report shows four zone bars — Heel, Midfoot, Ball, Toe — with a percentage. Each value is **the share of total foot pressure that zone carried during the run**, not raw ADC values. The four percentages sum to 100% (e.g., Heel 38%, Midfoot 19%, Ball 28%, Toe 15%). This is biomechanically meaningful — you can see "this runner heel-strikes hard" or "this runner is forefoot-dominant" at a glance.

## Thresholds are hardcoded

Dao's UI does not let users tune injury-flag thresholds. The thresholds (cadence < 160 spm, GCT > 300 ms, pronation > 15°, supination < −8°, loading-rate > 60 BW/s, asymmetry > 10%) come from peer-reviewed biomechanics research (sources in `SOLESENSE.md` §13) and shouldn't be a per-user setting. The firmware still has a `/api/settings` endpoint and an NVS-backed thresholds struct for future tooling, but no UI is wired to it.

## Known limitations (v0.1)

- **Andony's UI loads Google Fonts** via `@import` — fails on the SoleSense AP (no internet) and falls back to system fonts.
- **Andony's recording is client-side only** at 5 Hz, in browser memory. The firmware's 50 Hz LittleFS recording isn't wired into Andony's UI.
- **The dao and andony layouts cover overlapping but not identical feature sets.** Pick one as canonical for v0.2 and merge the missing pieces in.
- **Both UIs hold some run state in the browser** (Andony more, Dao less). v0.2 will move all run state onto the MCU — see [`docs/superpowers/specs/2026-05-06-v0.2-data-architecture.md`](../../docs/superpowers/specs/2026-05-06-v0.2-data-architecture.md). After that work, the JS sample arrays, the JS-side timer, and the JS-built CSV export all go away; the page becomes a thin viewer that re-queries the MCU on every reconnect.
