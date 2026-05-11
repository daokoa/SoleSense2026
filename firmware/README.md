# SoleSense Firmware

Two firmware paths in this repo. Both target the **Seeed XIAO ESP32-C3**, the same pinout, the same hardware.

| Folder | Build system | Notes |
|---|---|---|
| [`SoleSenseV2/`](SoleSenseV2/) | Arduino IDE | **Canonical firmware.** 500 Hz sampling, modular `.h/.cpp` layout, on-MCU step detector + GCT + FSR-jerk loading rate, incremental Goertzel FFT (1024-sample window), top-N outlier buffer, multi-slot crash-recoverable flash storage, pause-on-disconnect, NVS-backed user accounts, captive-portal-free mDNS hostname. Sensor layout is 3-zone x medial/lateral (Choi 2024 +E-at-heel). |
| [`platformio/`](platformio/) | PlatformIO | **Stub.** ~76-line `src/main.cpp` returning dummy random sensor data, alt SSID (`XIAO-ESP32` / pw `12345678`), ArduinoOTA. Not currently used. Keep around for the day we migrate the build off Arduino IDE. |

## Why v0.2 replaced v0.1

The first iteration (a single-file `SoleSense/SoleSense.ino`) streamed raw 50 Hz CSV samples to LittleFS and did the analysis in JavaScript in the browser after `/api/stop`. It worked but had three structural problems that we couldn't fix without a rewrite:

1. **Browser-side state was fragile.** A WiFi blip or page reload mid-run lost the in-progress recording.
2. **No on-device analysis.** Cadence / GCT / loading-rate calculations only ran when the phone fetched the CSV, so the device couldn't show anything live and couldn't keep running if the phone disconnected.
3. **No room for richer DSP.** Storing raw samples capped sample rate at ~50 Hz; we needed 500 Hz to capture impact rising edges cleanly for FSR-jerk extrapolation of loading rate.

`SoleSenseV2/` keeps every byte of run state on the MCU, stores FFT magnitudes + outliers + Welford stats in a CRC-protected ring buffer (not raw samples), and runs the full analysis on-device so the browser is just a renderer. See [`docs/superpowers/specs/2026-05-06-v0.2-data-architecture.md`](../docs/superpowers/specs/2026-05-06-v0.2-data-architecture.md) for the full rationale.

## Sensor layout

Six FSRs, three zones, two sensors per zone:

| Channel | Position |
|---|---|
| ch0 | Heel medial |
| ch1 | Heel lateral |
| ch2 | Midfoot medial |
| ch3 | Midfoot lateral |
| ch4 | Forefoot medial (under 1st MT) |
| ch5 | Forefoot lateral (under 5th MT) |

## What's flashed on the board right now

```bash
curl --noproxy '*' -s http://192.168.4.1/api/device | python3 -m json.tool | head -3
```

`firmware` should read `SoleSense v0.2-dev` and `sampleRateHz: 500`.

## How to flash

```bash
ARDUINO_CLI="/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"
"$ARDUINO_CLI" compile --fqbn "esp32:esp32:XIAO_ESP32C3" firmware/SoleSenseV2
"$ARDUINO_CLI" upload  --fqbn "esp32:esp32:XIAO_ESP32C3" --port /dev/cu.usbmodem2101 firmware/SoleSenseV2
bash firmware/SoleSenseV2/flash-littlefs.sh
```

LittleFS data: `firmware/SoleSenseV2/data/index.html` should be a copy of `software/frontend/solesense-v2/index.html`.

> **Heads-up:** the Arduino IDE auto-respawns its Serial Monitor whenever the XIAO re-enumerates after a flash, which holds the port and breaks the next upload. Close the Serial Monitor pane before flashing, or run `kill $(lsof -t /dev/cu.usbmodem*)` between attempts.

### PlatformIO (not currently used)

```bash
cd firmware/platformio
pio run --target upload
pio run --target uploadfs
pio device monitor
```

## Hardware setup

Pin map and matrix-scan FSR layout are documented in the [root README](../README.md). Key constants live in `firmware/SoleSenseV2/config.h`.

## Known caveats

1. **Loading rate** assumes a 70 kg body weight only when no user is logged in -- once a user signs in, their `body_kg` from the profile is used. The other calibration assumption (FSR full-scale ADC = 10 kg of force) is still hardcoded; future work is per-FSR saturation calibration.
2. **Step detector** uses fixed ADC thresholds gated by a 2-state Kalman filter (`R=25`, `Q` tuned for impact transients). FSR baseline drift from sweat / temperature is handled by the velocity-based toe-off, not absolute thresholds.
3. **End-to-end hardware verification** -- bench-tested in pieces; a 30-second real-run test on a fully-wired insole is the open follow-up.
4. **Step-detector debug Serial.printf** still in the build (handy for bring-up). Strip before clean release.

## Cross-references

- v0.2 architecture spec: [`../docs/superpowers/specs/2026-05-06-v0.2-data-architecture.md`](../docs/superpowers/specs/2026-05-06-v0.2-data-architecture.md)
- v0.2 implementation plan: [`../docs/superpowers/plans/2026-05-06-v0.2-firmware.md`](../docs/superpowers/plans/2026-05-06-v0.2-firmware.md)
- Profile / auth spec: [`../docs/superpowers/specs/2026-05-09-profile-system.md`](../docs/superpowers/specs/2026-05-09-profile-system.md)
