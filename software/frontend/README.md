# SoleSense Frontend

`index.html` is the **source-of-truth** for the SoleSense web app — a self-contained single-page app that runs on the user's phone or laptop, served by the XIAO ESP32-C3 over its `SoleSense` WiFi AP at `http://192.168.4.1`.

## Edit here

When you change the frontend, edit [`index.html`](index.html) in this folder. Don't edit the copy under `firmware/SoleSense/data/` directly — it's a deployment artifact, not the source.

## Run the frontend locally without the device

`mock-server.py` simulates the SoleSense firmware API with synthetic sensor data so you can preview the UI in any browser. From the repo root:

```bash
python3 software/frontend/mock-server.py
```

Then open http://localhost:8080/ — the frontend connects to the mock backend, polls `/api/sensor`, and shows live (fake) FSR + IMU values that look like a runner's stride. Useful for slide demos when the XIAO isn't plugged in. Ctrl+C to stop.

## Sync before flashing

The Arduino IDE LittleFS upload plugin uploads from `<sketch_dir>/data/`, so before each LittleFS flash the firmware-side copy needs to be in sync. From the repo root:

```bash
cp software/frontend/index.html firmware/SoleSense/data/index.html
```

Then in Arduino IDE: close Serial Monitor → `Cmd+Shift+P` → `Upload LittleFS to Pico/ESP8266/ESP32`.

## What it talks to

The frontend polls `GET /api/sensor` at 5 Hz to populate the live FSR bars and IMU readout. The full HTTP API surface is documented in the root [`README.md`](../../README.md#api).

## Known limitations (v0.1)

- Loads Google Fonts via `@import` — fails when on the SoleSense AP (no internet) and falls back to system fonts. Inline as base64 for v0.2.
- Recording is client-side only (5 Hz polling, JS in-memory, JS-built CSV export). The firmware backend has its own 50 Hz recording path via `POST /api/start` / `/api/stop` / `GET /data.csv` — those endpoints are not yet wired into this UI.
- Calibration endpoints (`POST /api/calibrate/zero` and `/api/calibrate/imu`) are not exposed in the UI. Hit them via `curl` for now.
