# SoleSense Firmware v0.2

The post-demo architecture: all run state lives on the MCU, no browser-side analysis, crash-recoverable flash storage. Now sampling at **500 Hz** with a **3-zone × medial/lateral sensor layout** (Choi 2024 with FSR E moved next to the heel).

> v0.1 still lives at [`../SoleSense/`](../SoleSense/) for the live demo.
> v0.2 has now reached feature-parity on the metrics that matter (step count, cadence, ground-contact-time, loading rate). It is the path forward.

## Background

- **Architecture spec:** [`../../docs/superpowers/specs/2026-05-06-v0.2-data-architecture.md`](../../docs/superpowers/specs/2026-05-06-v0.2-data-architecture.md)
- **Implementation plan:** [`../../docs/superpowers/plans/2026-05-06-v0.2-firmware.md`](../../docs/superpowers/plans/2026-05-06-v0.2-firmware.md)
- **Frontend (v0.2-aware viewer):** [`../../software/frontend/dao-v2/index.html`](../../software/frontend/dao-v2/index.html)

## Sensor layout

Six FSRs, three zones, two sensors per zone (one medial, one lateral). Drawn from Choi et al. 2024 ("Calibrating Low-Cost Smart Insole Sensors with Recurrent Neural Networks for Accurate Prediction of Center of Pressure", *Sensors* 24(15):4765) with FSR E relocated next to F so both sit at the heel.

| Channel | Position | Notes |
|---|---|---|
| ch0 | Heel medial | Inside-back of heel pad |
| ch1 | Heel lateral | Outside-back of heel pad |
| ch2 | Midfoot medial | Inside-middle, under the arch |
| ch3 | Midfoot lateral | Outside-middle |
| ch4 | Forefoot medial | Under 1st MT (just below hallux base) |
| ch5 | Forefoot lateral | Under 5th MT, near pinky toe |

The matrix-scan wiring (2 power sets × 3 ADC pins) is unchanged — only the physical FSR positions differ. See [`../README.md#pin-map`](../README.md) for the wiring detail.

## Module status

| Module | File(s) | Status |
|---|---|---|
| Identity / constants | `config.h` | ✅ done — sample rate 500 Hz |
| Run state machine + pause-on-disconnect | `state.h` / `state.cpp` | ✅ done |
| Sensor reads (matrix-scan FSR + I²C IMU) | `sensors.h` / `sensors.cpp` | ✅ done |
| Outlier min-heap (top-N by σ) | `outliers.h` / `outliers.cpp` | ✅ done |
| Per-channel Welford stats | `stats.h` / `stats.cpp` | ✅ done |
| 500 Hz hardware-timer sample loop | `SoleSenseV2.ino` | ✅ done |
| Goertzel FFT, 1024-sample window, 0.49 Hz bin width | `fft.h` / `fft.cpp` | ✅ done — bins redistributed for 500 Hz Nyquist (cadence + impact band + vibration probe) |
| Multi-slot ring-buffer flash storage | `storage.h` / `storage.cpp` | ✅ done — crash recovery validated via `/api/storage-selftest` |
| Time-domain step detector | `state.cpp` | ✅ done — Schmitt trigger on heel composite (max ch0/ch1), 400 ADC rise / 200 ADC fall, 150 ms refractory |
| Ground-contact-time | `state.cpp` | ✅ done — heel-strike → toe-off interval, [50, 800] ms valid range |
| HTTP routes (full set) | `http_routes.cpp` | ✅ done |
| FSR-jerk loading rate (BW/s) | `http_routes.cpp` + `SoleSenseV2.ino` | ✅ done — peak heel d(ADC)/dt × (98.1/4095/686.7) BW/s |
| Deep sleep + GPIO9 wake | `SoleSenseV2.ino` | ✅ done (older esp-idf API for portability) |

## What works on hardware

If you flash v0.2 onto a wired XIAO:

- AP comes up (`SoleSense` / `solesense`), all routes respond
- Recording state machine starts/stops; pauses when AP loses all clients
- 500 Hz sampling routes each sample through outlier detection → FFT → step detector
- Step count increments per heel strike, cadence = steps × 60 / runtime
- Ground contact time averages valid heel-strike-to-toe-off intervals
- Loading rate reports BW/s from peak heel d(ADC)/dt, gated on a 70 kg assumed body weight (placeholder until `/api/settings` exposes a user-set value)
- FFT magnitudes populate per channel/bin (validated against Python reference, 0% on-bin error)
- Storage flushes a CRC-protected snapshot every 3 s into a 10-slot ring; corrupted slots correctly rejected on load
- `/api/run-state` reports live elapsed time (sourced from latest valid flash slot, not wall clock — pauses honestly during disconnect)
- `/api/run-report` returns step count, cadence, contactMs, loadingRate (BW/s), pronation, zone breakdown (heel / midfoot / forefoot), medial/lateral split, and the injury-flag set
- `/api/storage-selftest` and `/api/fft-selftest` provide on-bench validation

## Known caveats

These are *real numbers reported with explicit assumptions* — not stubs.

1. **Loading-rate body-weight assumption.** The BW/s conversion divides by 686.7 N (= 70 kg × g). For a 50 kg or 90 kg user the true BW/s scales by 70/their_weight. Plan: expose body weight via `/api/settings` and persist to NVS.
2. **Loading-rate FSR saturation assumption.** The conversion assumes the FSR + voltage-divider hits ADC=4095 at exactly 10 kg of force. Actual saturation point depends on the divider resistor; it could be anywhere from ADC=2500 to 4095 in practice. Calibration step needed: measure the ADC reading at a known applied force (e.g., 5 kg) and fit a per-divider scaling factor.
3. **Step detector debug logs are still on.** `step_detector_update()` in `state.cpp` prints `[Step] strike #N` and `[Step] toeoff` to Serial whenever it fires. Useful for hardware bring-up; remove for clean release builds.
4. **Hardware verification gap.** The state-machine, AP, sampling-loop, FFT, storage, pause-on-disconnect, step detector, and loading-rate paths have all been bench-tested. The full "30-second run with rhythmic foot strikes" end-to-end test has not been recorded yet.

## Compile / flash

Sketch upload via the bundled arduino-cli:

```bash
ARDUINO_CLI="/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"
"$ARDUINO_CLI" compile --fqbn "esp32:esp32:XIAO_ESP32C3" firmware/SoleSenseV2
"$ARDUINO_CLI" upload  --fqbn "esp32:esp32:XIAO_ESP32C3" --port /dev/cu.usbmodem2101 firmware/SoleSenseV2
```

Or open `SoleSenseV2.ino` in Arduino IDE and click Upload — the IDE picks up all the `.h`/`.cpp` files in the same folder automatically. **Close the Serial Monitor before uploading or the port will be busy.**

LittleFS data (the v0.2-aware frontend):

```bash
cp software/frontend/dao-v2/index.html firmware/SoleSenseV2/data/index.html
bash firmware/SoleSenseV2/flash-littlefs.sh
```

After flashing, verify:

```bash
curl --noproxy '*' -s http://192.168.4.1/api/device | python3 -m json.tool | head -3
```

`firmware` should read `SoleSense v0.2-dev` and `sampleRateHz` should read `500`.

## Tasks for new contributors

In priority order:

1. **Body-weight + FSR-saturation calibration** for the loading-rate metric. Add a `body_weight_kg` field to `/api/settings` (currently a stub), persist to NVS, divide loadingRateBWs by `body_weight_kg × g` instead of the hardcoded 686.7. Fit per-FSR saturation slopes via a brief calibration step (apply known weights, capture ADC).
2. **Hardware verification.** 30 s real-run test on a XIAO with the wired insole. Confirm cadence detection, pressure distribution, contact time, and loading rate all read sensible numbers under real running.
3. **Time-domain GCT robustness.** Current detector uses a fixed-threshold Schmitt trigger. If the FSR baseline drifts (sweat, temperature) the threshold may need to be EMA-tracked. The `step_detector_update()` signature already takes `heelMean`/`heelStddev` for exactly this purpose; wiring up a slow EMA baseline is the next step.
4. **Strip Serial debug.** Remove the `[Step] strike` / `[Step] toeoff` prints from `state.cpp` once hardware confidence is high.
