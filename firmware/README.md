# SoleSense Firmware

This directory contains **two parallel firmware implementations** for the XIAO ESP32-C3. Both target the same board and same hardware. They differ in build system and current completeness.

| Folder | Build system | Status |
|---|---|---|
| [`SoleSense/`](SoleSense/) | **Arduino IDE** | **v0.1 — working, flashed and verified.** Demo firmware. ~520 lines in a single `.ino`. Raw 50 Hz CSV → LittleFS → browser-side analysis. The frontend served from LittleFS lives at `SoleSense/data/index.html` — sync the chosen UI from `software/frontend/{dao,andony}/` before flashing. |
| [`SoleSenseV2/`](SoleSenseV2/) | **Arduino IDE** | **v0.2 — work in progress.** Modular rewrite based on the [v0.2 architecture spec](../docs/superpowers/specs/2026-05-06-v0.2-data-architecture.md): incremental Goertzel FFT + top-N outlier buffer in MCU RAM, multi-slot ring buffer in flash, pause-on-disconnect, no browser-side state. Foundational scaffold compiles and boots; FFT, storage, and `/api/run-report` are stubbed. See `SoleSenseV2/README.md` for module-level status. |
| [`platformio/`](platformio/) | **PlatformIO** | Stub. ~76-line `src/main.cpp` returning dummy random sensor data, with a different SSID (`XIAO-ESP32` / pw `12345678`) and ArduinoOTA support. Starting point for the team if you want to migrate to a PlatformIO/VS Code workflow. |

---

## Which one is on the board right now?

`SoleSense/` (v0.1). Connect to WiFi `SoleSense` (password `solesense`), open `http://192.168.4.1` — that's what's running. Demo-ready.

## Which one should new code go into?

- **For demo-affecting fixes (tonight)** → `SoleSense/`. Don't disturb v0.1.
- **For the longer-term rewrite** → `SoleSenseV2/`. New modules; foundational scaffold is already there. Pick up Task 4 (FFT), Task 5 (storage), or Task 6 (run-report analysis) from [`../docs/superpowers/plans/2026-05-06-v0.2-firmware.md`](../docs/superpowers/plans/2026-05-06-v0.2-firmware.md).

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
