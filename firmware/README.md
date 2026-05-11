# SoleSense Firmware

Three parallel firmware paths in this repo. All target the **Seeed XIAO ESP32-C3**, the same pinout, the same hardware.

| Folder | Build system | Status | Use this when |
|---|---|---|---|
| [`SoleSense/`](SoleSense/) | Arduino IDE | **v0.1 — DEMO-READY.** ~520 lines, single `.ino`, raw 50 Hz CSV → LittleFS, browser-side JS analysis. Fully verified on hardware. | You need a working device for the live demo. |
| [`SoleSenseV2/`](SoleSenseV2/) | Arduino IDE | **v0.2 — feature-complete on metrics.** Modular rewrite. **500 Hz sampling**, on-MCU step detector + GCT + FSR-jerk loading rate, incremental Goertzel FFT (1024-sample window), top-N outlier buffer, multi-slot crash-recoverable flash storage, pause-on-disconnect, no browser-side state. Sensor layout is 3-zone × medial/lateral (Choi 2024 +E-at-heel). | You're working on the post-demo architecture or running with the new layout. |
| [`platformio/`](platformio/) | PlatformIO | **Stub.** ~76-line `src/main.cpp` returning dummy random sensor data, alt SSID (`XIAO-ESP32` / pw `12345678`), ArduinoOTA. Not currently used. | You want to migrate the build to PlatformIO + VS Code. Port one of the other paths in. |

## Sensor layout (v0.2)

Six FSRs, three zones, two sensors per zone:

| Channel | Position |
|---|---|
| ch0 | Heel medial |
| ch1 | Heel lateral |
| ch2 | Midfoot medial |
| ch3 | Midfoot lateral |
| ch4 | Forefoot medial (under 1st MT) |
| ch5 | Forefoot lateral (under 5th MT) |

v0.1 still uses the older 1-heel + 2-midfoot + 2-ball + 1-toe layout. If you switch between firmwares, the *physical* sensors stay where they are — only the channel-to-zone aggregation changes.

## What's flashed on the board right now?

The most-recently-flashed sketch + LittleFS combo wins. To check:

```bash
curl --noproxy '*' -s http://192.168.4.1/api/device | python3 -m json.tool | head -3
```

`firmware` field will say `SoleSense v0.1` or `SoleSense v0.2-dev`. v0.2 also reports `sampleRateHz: 500`.

## How to flash each version

### v0.1 (demo)

Sketch — Arduino IDE, or terminal via the bundled arduino-cli:

```bash
ARDUINO_CLI="/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"
"$ARDUINO_CLI" compile --fqbn "esp32:esp32:XIAO_ESP32C3" firmware/SoleSense
"$ARDUINO_CLI" upload  --fqbn "esp32:esp32:XIAO_ESP32C3" --port /dev/cu.usbmodem2101 firmware/SoleSense
```

LittleFS:
```bash
bash firmware/SoleSense/flash-littlefs.sh
```

### v0.2 (current development)

Same idea but pointed at `SoleSenseV2/`:

```bash
"$ARDUINO_CLI" compile --fqbn "esp32:esp32:XIAO_ESP32C3" firmware/SoleSenseV2
"$ARDUINO_CLI" upload  --fqbn "esp32:esp32:XIAO_ESP32C3" --port /dev/cu.usbmodem2101 firmware/SoleSenseV2
bash firmware/SoleSenseV2/flash-littlefs.sh
```

LittleFS data: `firmware/SoleSenseV2/data/index.html` should be a copy of `software/frontend/solesense-v2/index.html` (the v0.2-aware frontend that polls `/api/run-state` and `/api/run-report` instead of running JS analysis on a CSV).

> **Heads-up:** the Arduino IDE auto-respawns its Serial Monitor whenever the XIAO re-enumerates after a flash, which holds the port and breaks the next upload. Close the Serial Monitor pane before flashing, or run `kill $(lsof -t /dev/cu.usbmodem*)` between attempts.

### PlatformIO (not currently used)

```bash
cd firmware/platformio
pio run --target upload     # sketch
pio run --target uploadfs   # LittleFS (needs platformio/data/index.html)
pio device monitor
```

## Hardware setup (same across all paths)

Pin map and matrix-scan FSR layout are documented in the [root README](../README.md). Key constants live in:

- v0.1: top of `firmware/SoleSense/SoleSense.ino`
- v0.2: `firmware/SoleSenseV2/config.h`

## v0.2 known caveats

The metrics-side stubs are gone, but a couple of calibration assumptions remain:

1. **Loading rate** assumes a 70 kg body weight and the FSR's full-scale ADC = 10 kg of force. Real BW/s scales by `70 / actual_kg` and by `actual_FSR_saturation_ADC / 4095`. Both will become user-configurable via `/api/settings` later.
2. **Step detector** uses fixed ADC thresholds (400 rise, 200 fall). FSR baseline drift (sweat, temp) could push these out of range; the EMA-baseline path is wired in (`heelMean`/`heelStddev` are already passed to `step_detector_update()`) but unused for now.
3. **End-to-end hardware verification** — bench-tested in pieces; a 30-second real-run test is the open follow-up.
4. **Step-detector debug Serial.printf** still in the build (handy for bring-up). Strip before clean release.

## Cross-references

- v0.1 spec: [`../docs/superpowers/specs/2026-05-04-solesense-firmware-design.md`](../docs/superpowers/specs/2026-05-04-solesense-firmware-design.md)
- v0.1 plan: [`../docs/superpowers/plans/2026-05-04-solesense-firmware-v0.1.md`](../docs/superpowers/plans/2026-05-04-solesense-firmware-v0.1.md)
- v0.2 architecture spec: [`../docs/superpowers/specs/2026-05-06-v0.2-data-architecture.md`](../docs/superpowers/specs/2026-05-06-v0.2-data-architecture.md)
- v0.2 implementation plan: [`../docs/superpowers/plans/2026-05-06-v0.2-firmware.md`](../docs/superpowers/plans/2026-05-06-v0.2-firmware.md)
