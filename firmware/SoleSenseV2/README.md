# SoleSense Firmware v0.2 (work-in-progress)

The post-demo architecture: all run state lives on the MCU, no browser-side analysis, crash-recoverable flash storage. Compiles and boots; hardware-verified for the boot/AP/recording state machine paths but **not yet a drop-in replacement for v0.1**.

> **For the demo, keep flashing v0.1** (`firmware/SoleSense/`).
> v0.2 is what you migrate to once the open issues below close.

## Background

- **Architecture spec:** [`../../docs/superpowers/specs/2026-05-06-v0.2-data-architecture.md`](../../docs/superpowers/specs/2026-05-06-v0.2-data-architecture.md)
- **Implementation plan:** [`../../docs/superpowers/plans/2026-05-06-v0.2-firmware.md`](../../docs/superpowers/plans/2026-05-06-v0.2-firmware.md)
- **Frontend (v0.2-aware viewer):** [`../../software/frontend/dao-v2/index.html`](../../software/frontend/dao-v2/index.html)

## Module status

| Module | File(s) | Status |
|---|---|---|
| Identity / constants | `config.h` | ✅ done |
| Run state machine + pause-on-disconnect | `state.h` / `state.cpp` | ✅ done |
| Sensor reads (matrix-scan FSR + I²C IMU) | `sensors.h` / `sensors.cpp` | ✅ done |
| Outlier min-heap (top-N by σ) | `outliers.h` / `outliers.cpp` | ✅ done |
| Per-channel Welford stats | `stats.h` / `stats.cpp` | ✅ done |
| 50 Hz hardware-timer sample loop + jerk tracker | `SoleSenseV2.ino` | ✅ done |
| Goertzel FFT (validated 0.0% on-bin error) | `fft.h` / `fft.cpp` | ✅ done |
| Multi-slot ring-buffer flash storage | `storage.h` / `storage.cpp` | ✅ done — crash recovery validated via `/api/storage-selftest` |
| HTTP routes (`/api/start /stop /sensor /device /sleep /calibrate/*`) | `http_routes.cpp` | ✅ done |
| HTTP routes (`/api/run-state /run-spectrum /run-outliers /run-report`) | `http_routes.cpp` | ✅ shape-correct; `/api/run-report` numbers honest where measurable, `0`/null where not |
| Loading-rate computation | `http_routes.cpp` (run-report) | ⚠️ **stub — returns 0.** See open issues below. |
| Ground-contact-time computation | `http_routes.cpp` (run-report) | ⚠️ **stub — returns 0.** Needs time-domain step detection. |
| Deep sleep + GPIO9 wake | `SoleSenseV2.ino` | ✅ done (older esp-idf API for portability) |

## What works on hardware right now

If you flash v0.2 onto the XIAO:

- AP comes up, all routes respond
- Recording state machine starts/stops; pauses when AP loses all clients
- 50 Hz sampling routes each sample through outlier detection → FFT
- FFT magnitudes populate per channel/bin (validated against Python reference)
- Storage flushes a CRC-protected snapshot every 3 s into a 10-slot ring; corrupted slots correctly rejected on load
- `/api/run-state` reports live elapsed time (sourced from latest valid flash slot, not wall clock — pauses honestly during disconnect)
- `/api/run-report` returns real cadence, pressure distribution, medial/lateral split, pronation, and the injury-flag set; honest "—" for the metrics we can't measure yet
- `/api/storage-selftest` and `/api/fft-selftest` provide on-bench validation of those subsystems

## Open issues blocking the demo migration

These are the reasons v0.1 is still flashed for the live demo:

### 1. Loading-rate calculation is stubbed (returns 0)

The FSR 402 saturates at ~10 kg of force. Running impacts deliver 100–200 kg of ground-reaction force, which the FSR can't directly measure. **The right fix is FSR-jerk extrapolation:** track the rate-of-rise of the FSR signal during the brief unsaturated portion of the impact transient; that derivative carries the impact-magnitude information even though the saturated peak is clipped.

> An earlier attempt computed loading rate from IMU vertical-jerk (`d(accel_z)/dt`). That measures torso/insole acceleration, not the FSR's force ramp, so it was the wrong axis. Reverted; the IMU jerk is still tracked in the per-sample loop (`gMaxJerkZ`) and remains available if a future implementation wants to fuse FSR-jerk + IMU-jerk.

Until FSR-jerk lands, `/api/run-report` returns `loadingRate: 0.0`, the chip on the recording screen reads "—", and the high-loading injury flag never fires. Honest > fake.

### 2. Ground-contact-time is stubbed (returns 0)

GCT requires per-stride heel-strike-to-toe-off detection in the time domain. v0.2 deliberately doesn't keep raw samples (storage architecture is FFT + outliers only), so we can't compute GCT at report time.

Plan: add a small circular sample buffer (~5 s, ~250 samples × 4 bytes/channel) that the run-report handler can use to detect strides. Scope is small but hasn't landed.

### 3. Hardware verification gap

The state-machine, AP, sampling-loop, FFT, storage, and pause-on-disconnect paths have all been bench-tested on a real XIAO with a single FSR pressing. The full "30-second run with rhythmic foot strikes producing realistic cadence/pressure/flag values" test hasn't run yet.

## Compile / flash

Sketch upload via the bundled arduino-cli:

```bash
ARDUINO_CLI="/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"
"$ARDUINO_CLI" compile --fqbn "esp32:esp32:XIAO_ESP32C3" firmware/SoleSenseV2
"$ARDUINO_CLI" upload  --fqbn "esp32:esp32:XIAO_ESP32C3" --port /dev/cu.usbmodem2101 firmware/SoleSenseV2
```

Or open `SoleSenseV2.ino` in Arduino IDE and click Upload — the IDE picks up all the `.h`/`.cpp` files in the same folder automatically.

LittleFS data (the v0.2-aware frontend):

```bash
cp software/frontend/dao-v2/index.html firmware/SoleSenseV2/data/index.html
bash firmware/SoleSenseV2/flash-littlefs.sh
```

After flashing, verify:

```bash
curl --noproxy '*' -s http://192.168.4.1/api/device | python3 -m json.tool | head -3
```

`firmware` should read `SoleSense v0.2-dev`.

## Tasks for new contributors

In priority order:

1. **FSR-jerk loading-rate extrapolation** (issue #1 above). Implement in `http_routes.cpp` `handle_run_report`. Track the per-sample rate-of-rise of FSR signal in `process_sample` (similar shape to the IMU jerk tracker that's already in `SoleSenseV2.ino`). Set the high-loading flag threshold once you have real data to calibrate against.
2. **Time-domain step detection / GCT** (issue #2). New module: `samplebuffer.h` / `.cpp` — a circular buffer of the last N samples per channel. Step-detection logic in the run-report handler.
3. **Hardware verification** (issue #3). 30 s real-run test on a XIAO with the wired FSR. Confirm cadence detection works, pressure distribution makes anatomical sense, storage survives a power-cut mid-run.
