# SoleSense Firmware

This directory contains **two parallel firmware implementations** for the XIAO ESP32-C3. Both target the same board and same hardware. They differ in build system and current completeness.

| Folder | Build system | Status | Description |
|---|---|---|---|
| [`SoleSense/`](SoleSense/) | **Arduino IDE** | **Working — flashed and verified** | The v0.1 firmware actively running on the board. ~520 lines in a single `.ino`, all 10 HTTP endpoints, 50 Hz hardware-timer sampling of 6 FSRs + MPU-6050, NVS-backed thresholds + sensor calibration, GPIO9 deep-sleep. Frontend SPA served from `SoleSense/data/index.html` via the LittleFS upload plugin. |
| [`platformio/`](platformio/) | **PlatformIO** | Stub — not yet flashed | Andony's parallel implementation. Currently a 76-line `src/main.cpp` that returns dummy random sensor data, hosts a different SSID (`XIAO-ESP32` / password `12345678`), and includes ArduinoOTA support. Useful starting point for whoever wants to migrate the firmware to PlatformIO + VS Code workflow. |

---

## Which one is on the board right now?

The Arduino IDE firmware (`SoleSense/SoleSense.ino`). When you connect to the WiFi SSID `SoleSense` (password `solesense`) at `http://192.168.4.1`, that's what's running.

## Which one should new code go into?

Until the team picks one as canonical: **add new firmware features to `SoleSense/SoleSense.ino`** since that's the path that actually runs on hardware. If you want to migrate everything to PlatformIO instead, port the working Arduino implementation into `platformio/src/main.cpp` first, then delete the Arduino sketch.

## Why both exist

- The Arduino path was built first, end-to-end, to hit the v0.1 demo deadline. See [`docs/superpowers/specs/2026-05-04-solesense-firmware-design.md`](../docs/superpowers/specs/2026-05-04-solesense-firmware-design.md) and the implementation plan alongside it.
- The PlatformIO path was scaffolded in parallel with a simpler stub for VS Code-based development.

Pick one before v0.2 — supporting two divergent firmwares long-term is a maintenance trap.

## Build / flash quick reference

### Arduino IDE (`SoleSense/`)

See the root [`README.md`](../README.md#setup--flashing-arduino-ide) for full setup. TL;DR:
1. Open `SoleSense/SoleSense.ino`
2. Set Tools → Board → ESP32C3 Dev Module, USB CDC On Boot: Enabled, Partition: 1.5 MB SPIFFS
3. Click Upload (`→`)
4. Close Serial Monitor → `Cmd+Shift+P` → Upload LittleFS

### PlatformIO (`platformio/`)

```bash
cd firmware/platformio
pio run --target upload          # build + flash sketch
pio run --target uploadfs        # upload data/ folder to LittleFS (you'll need to create platformio/data/ with the frontend first)
pio device monitor               # serial monitor
```

Note: `platformio/` does not currently have a `data/` folder. If you flash this firmware and want a frontend served, copy `SoleSense/data/index.html` into `platformio/data/index.html` first.
