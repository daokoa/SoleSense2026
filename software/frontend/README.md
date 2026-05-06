# SoleSense Frontend

Two parallel UIs for the SoleSense web app, kept side by side. Both connect to the same firmware HTTP API; only the visual design and code differ.

```
software/frontend/
├── README.md           ← this file
├── mock-server.py      ← Python http.server that simulates the firmware backend
├── dao/
│   └── index.html      ← Dao's UI: white background, blue accent, minimal API panel
└── andony/
    └── index.html      ← Andony's UI: dark theme, animated orbs, full SPA
```

Each UI is a self-contained `index.html` (no external assets except Andony's Google Fonts import). Both are served by the XIAO ESP32-C3 over its `SoleSense` WiFi AP at `http://192.168.4.1` — but only one at a time, since LittleFS only stores one `index.html`.

## Which UI is currently flashed

The active UI lives at `firmware/SoleSense/data/index.html` (it's the file the Arduino LittleFS plugin uploads). **Right now that's a copy of `dao/index.html`** — Dao's white/blue UI.

## Switch the active UI

Edit `firmware/SoleSense/data/index.html` to be whichever you want, then re-upload LittleFS. Easiest:

```bash
# Use Dao's UI:
cp software/frontend/dao/index.html firmware/SoleSense/data/index.html

# Use Andony's UI:
cp software/frontend/andony/index.html firmware/SoleSense/data/index.html
```

Then in Arduino IDE: close Serial Monitor → `Cmd+Shift+P` → `Upload LittleFS to Pico/ESP8266/ESP32`.

## Preview a UI locally without flashing

`mock-server.py` simulates the firmware backend with synthetic stride-shaped sensor data so you can preview either UI in any browser:

```bash
# From the repo root:
python3 software/frontend/mock-server.py            # serves dao/ (default)
python3 software/frontend/mock-server.py andony     # serves andony/
```

Then open `http://localhost:8080/`. The mock server implements the same HTTP API as the real firmware (per `SOLESENSE.md` §10), so the UI behaves identically to how it would on the device. Ctrl+C to stop.

## Edit each UI

Edit `software/frontend/dao/index.html` for the white/blue UI, `software/frontend/andony/index.html` for the dark SPA. Don't edit `firmware/SoleSense/data/index.html` directly — it's a deployment artifact that gets overwritten via the `cp` command above.

## What each UI talks to

Both UIs use a subset of the firmware's HTTP API. The full surface is documented in the root [`README.md`](../../README.md#api).

- **`dao/index.html`** — has buttons for every endpoint (`/api/start`, `/api/stop`, calibrate, settings, sleep, device, data clear, CSV download). Auto-fetches `/api/device` on load. Best for backend smoke testing.
- **`andony/index.html`** — polls `/api/sensor` at 5 Hz for the live FSR + IMU readout, runs recording client-side (5 Hz JS poll loop, in-memory samples, JS-built CSV export). Best for production-style demo.

## Known limitations (v0.1)

- **Andony's UI loads Google Fonts** via `@import` — fails on the SoleSense AP (no internet) and falls back to system fonts.
- **Andony's recording is client-side only** at 5 Hz. The firmware's 50 Hz LittleFS recording (`POST /api/start` / `/api/stop` / `GET /data.csv`) isn't wired into either UI yet.
- **Calibration endpoints** (`POST /api/calibrate/zero` and `/api/calibrate/imu`) are exposed in `dao/` but not `andony/`. Use Dao's UI or `curl` for calibration.
