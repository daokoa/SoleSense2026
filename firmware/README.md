# SoleSense Firmware

This directory contains **two parallel firmware implementations** for the XIAO ESP32-C3. Both target the same board and same hardware. They differ in build system and current completeness.

| Folder | Build system | Status |
|---|---|---|
| [`SoleSense/`](SoleSense/) | **Arduino IDE** | **Working — flashed and verified.** The v0.1 firmware actively running on the board. ~520 lines in a single `.ino`, all 10 HTTP endpoints, 50 Hz hardware-timer sampling of 6 FSRs + MPU-6050, NVS-backed thresholds + sensor calibration, GPIO9 deep-sleep. The frontend served from LittleFS lives at `SoleSense/data/index.html` — sync the chosen UI from `software/frontend/{dao,andony}/` before flashing. |
| [`platformio/`](platformio/) | **PlatformIO** | Stub. ~76-line `src/main.cpp` returning dummy random sensor data, with a different SSID (`XIAO-ESP32` / pw `12345678`) and ArduinoOTA support. Starting point for the team if you want to migrate to a PlatformIO/VS Code workflow. |

---

## Which one is on the board right now?

The Arduino IDE firmware (`SoleSense/SoleSense.ino`). Connect to WiFi `SoleSense` (password `solesense`), open `http://192.168.4.1` — that's what's running.

## Which one should new code go into?

Until the team picks a canonical path: **add new firmware features to `SoleSense/SoleSense.ino`**. That's the path that actually runs on hardware. To migrate to PlatformIO instead, port the Arduino implementation into `platformio/src/main.cpp` first, then delete the Arduino sketch.

## Why both exist

- The Arduino path was built first, end-to-end, to hit the v0.1 demo deadline. See [`../docs/superpowers/specs/2026-05-04-solesense-firmware-design.md`](../docs/superpowers/specs/2026-05-04-solesense-firmware-design.md) and the implementation plan alongside it.
- The PlatformIO path was scaffolded in parallel for VS Code-based development.

Pick one before v0.2 — supporting two divergent firmwares long-term is a maintenance trap.

---

## Build / flash quick reference

### Arduino IDE (`SoleSense/`) — sketch upload

Tools menu (set once):
- Board: `ESP32 Arduino → ESP32C3 Dev Module` (or `XIAO_ESP32C3`)
- USB CDC On Boot: `Enabled`
- Partition Scheme: `Default 4MB with spiffs (1.2MB APP / 1.5MB SPIFFS)`

Then:
1. Open `firmware/SoleSense/SoleSense.ino`
2. Click Upload (`→`)

### Arduino IDE — LittleFS data upload (the official way)

`Cmd+Shift+P` → `Upload LittleFS to Pico/ESP8266/ESP32` → Enter. Requires the [arduino-littlefs-upload plugin](https://github.com/earlephilhower/arduino-littlefs-upload) installed at `~/.arduinoIDE/plugins/`.

### Terminal — LittleFS data upload (the reliable way)

If the IDE plugin is flaky on your machine, use the bundled script. From the repo root:

```bash
bash firmware/SoleSense/flash-littlefs.sh
```

The script:
- Locates `mklittlefs` and `esptool` from your installed ESP32 board package (`~/Library/Arduino15/packages/esp32/tools/`)
- Builds the LittleFS image from `firmware/SoleSense/data/`
- Auto-detects the XIAO's USB port (override via `PORT=/dev/cu.usbmodemXXXX`)
- Flashes it at 921600 baud and verifies the hash

Heads up: close Arduino IDE Serial Monitor first — it locks the port.

### PlatformIO (`platformio/`)

```bash
cd firmware/platformio
pio run --target upload          # build + flash sketch
pio run --target uploadfs        # upload data/ folder to LittleFS
pio device monitor               # serial monitor
```

`platformio/` does not currently have a `data/` folder. If you want to flash this firmware with a frontend, copy one in first:

```bash
cp software/frontend/dao/index.html firmware/platformio/data/index.html
```
