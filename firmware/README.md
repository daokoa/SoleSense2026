# SoleSense Firmware

Three parallel firmware paths in this repo. All target the **Seeed XIAO ESP32-C3**, the same pinout, the same hardware.

| Folder | Build system | Status | Use this when |
|---|---|---|---|
| [`SoleSense/`](SoleSense/) | Arduino IDE | **v0.1 — DEMO-READY.** ~520 lines, single `.ino`, raw 50 Hz CSV → LittleFS, browser-side JS analysis. Fully verified on hardware. **This is what you flash for the live demo.** | You need a working device tonight. |
| [`SoleSenseV2/`](SoleSenseV2/) | Arduino IDE | **v0.2 — work-in-progress.** Modular rewrite (multiple `.h`/`.cpp`): incremental Goertzel FFT, top-N outlier buffer, multi-slot crash-recoverable flash storage, pause-on-disconnect, no browser-side state. All on-MCU analysis. Compiles and boots. Loading-rate calculation deliberately returns 0 right now (FSR-jerk extrapolation is the next implementation milestone — see Open issues below). | You're working on the post-demo architecture. |
| [`platformio/`](platformio/) | PlatformIO | **Stub.** ~76-line `src/main.cpp` returning dummy random sensor data, alt SSID (`XIAO-ESP32` / pw `12345678`), ArduinoOTA. Not currently used. | You want to migrate the build to PlatformIO + VS Code. Port one of the other paths in. |

## What's flashed on the board right now?

The most-recently-flashed sketch + LittleFS combo wins. To check:

```bash
curl --noproxy '*' -s http://192.168.4.1/api/device | python3 -m json.tool | head -3
```

`firmware` field will say `SoleSense v0.1` or `SoleSense v0.2-dev`.

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

### v0.2 (work-in-progress)

Same idea but pointed at `SoleSenseV2/`:

```bash
"$ARDUINO_CLI" compile --fqbn "esp32:esp32:XIAO_ESP32C3" firmware/SoleSenseV2
"$ARDUINO_CLI" upload  --fqbn "esp32:esp32:XIAO_ESP32C3" --port /dev/cu.usbmodem2101 firmware/SoleSenseV2
bash firmware/SoleSenseV2/flash-littlefs.sh
```

LittleFS data: `firmware/SoleSenseV2/data/index.html` should be a copy of `software/frontend/dao-v2/index.html` (the v0.2-aware frontend that polls `/api/run-state` and `/api/run-report` instead of running JS analysis on a CSV).

### PlatformIO (not currently used)

```bash
cd firmware/platformio
pio run --target upload     # sketch
pio run --target uploadfs   # LittleFS (needs platformio/data/index.html)
pio device monitor
```

## Hardware setup (same across all paths)

Pin map and matrix-scan FSR layout are documented in the [root README](../README.md#pin-map). Key constants live in:

- v0.1: top of `firmware/SoleSense/SoleSense.ino`
- v0.2: `firmware/SoleSenseV2/config.h`

## Open issues blocking v0.2 from replacing v0.1

1. **FSR-jerk loading-rate extrapolation.** FSR 402 caps at ~10 kg, but running impacts deliver 100–200 kg of ground-reaction force. The plan: track the rate-of-rise of the FSR signal *before* saturation as the impact-magnitude indicator, on a per-stride basis. v0.2 currently returns `loadingRate = 0` until this lands.
2. **Time-domain step detection.** Required for ground-contact-time. v0.2 keeps only the FFT spectrum + outliers; per-stride heel-strike-to-toe-off detection needs a circular sample buffer module.
3. **Hardware verification.** Compiles, boots, exposes all routes, but hasn't been run end-to-end through a real recorded run on hardware (only bench-tested with single FSR pressing).

Until 1 and 2 land, **stick with v0.1 for the demo**. v0.2 is for after.

## Cross-references

- v0.1 spec: [`../docs/superpowers/specs/2026-05-04-solesense-firmware-design.md`](../docs/superpowers/specs/2026-05-04-solesense-firmware-design.md)
- v0.1 plan: [`../docs/superpowers/plans/2026-05-04-solesense-firmware-v0.1.md`](../docs/superpowers/plans/2026-05-04-solesense-firmware-v0.1.md)
- v0.2 architecture spec: [`../docs/superpowers/specs/2026-05-06-v0.2-data-architecture.md`](../docs/superpowers/specs/2026-05-06-v0.2-data-architecture.md)
- v0.2 implementation plan: [`../docs/superpowers/plans/2026-05-06-v0.2-firmware.md`](../docs/superpowers/plans/2026-05-06-v0.2-firmware.md)
