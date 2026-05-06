# SoleSense v0.2 (work-in-progress)

The v0.2 firmware skeleton. **This is not yet flashable as a working replacement for v0.1** — it compiles and boots, brings up the WiFi AP, registers all the new HTTP routes, and runs an active recording loop, but the FFT and storage modules are stubbed (see status table below).

For the demo, keep flashing **`firmware/SoleSense/SoleSense.ino`** (v0.1).

For the full v0.2 design, read [`../../docs/superpowers/specs/2026-05-06-v0.2-data-architecture.md`](../../docs/superpowers/specs/2026-05-06-v0.2-data-architecture.md).

For the task breakdown, read [`../../docs/superpowers/plans/2026-05-06-v0.2-firmware.md`](../../docs/superpowers/plans/2026-05-06-v0.2-firmware.md).

## Module status

| Module | Files | Status |
|---|---|---|
| Identity / constants | `config.h` | ✅ done |
| State machine + pause-on-disconnect | `state.h/.cpp` | ✅ done |
| Sensor reads | `sensors.h/.cpp` | ✅ done (lifted from v0.1) |
| Outlier min-heap | `outliers.h/.cpp` | ✅ done |
| Per-sample online stats | `SoleSenseV2.ino` (Welford) | ✅ done |
| 50 Hz hardware timer + sample loop | `SoleSenseV2.ino` | ✅ done |
| Goertzel FFT | `fft.h/.cpp` | ✅ done — windowed (N=256), DC removal via running mean, magnitudes calibrated, validated against a Python reference (0% error on-bin). Trigger via `POST /api/fft-selftest`. |
| Multi-slot ring buffer storage | `storage.h/.cpp` | ⚠️ stub — file pre-allocated but save/load are no-ops |
| HTTP routes (simple) | `http_routes.cpp` | ✅ done — `/api/device`, `/api/sensor`, `/api/start`, `/api/stop`, `/api/sleep`, calibrate routes |
| HTTP routes (v0.2 new) | `http_routes.cpp` | ⚠️ partial — `/api/run-state`, `/api/run-spectrum`, `/api/run-outliers` shape-correct; `/api/run-report` returns placeholder values |
| Deep sleep | `SoleSenseV2.ino` | ✅ done |

## What flashing this gets you right now

If you uploaded this sketch to the XIAO instead of v0.1, it would:

- Boot, mount LittleFS, init sensors, bring up the SoleSense AP at 192.168.4.1
- Accept `/api/start` and `/api/stop`, run the 50 Hz sample loop, pause when no clients are on the AP, resume when they reconnect
- Compute online running-mean/stddev per channel, route samples through outlier detection vs. FFT
- Track up to 100 outliers per run with full history
- Return `/api/run-state` with live `recording`, `run_active`, `elapsed_ms`, `sample_count`, `clients_connected`, `outlier_count`

What it would **not** yet do:

- Persist anything to flash (the slot writer is a no-op until Task 5)
- Survive a reboot mid-run (no flash → no recovery)
- Compute injury flags for `/api/run-report` (returns placeholders — Task 6)
- Persist anything across reboot (Task 5 is still stubbed)

What it WILL now do (Task 4 just landed):

- Compute correctly-scaled FFT magnitudes per channel/bin every 256 samples (~5 s at 50 Hz)
- Expose `/api/run-spectrum` with real numbers per (channel, bin)
- Expose `/api/fft-selftest` for on-bench validation

## Compile / flash

The same Arduino IDE setup as v0.1. Open `firmware/SoleSenseV2/SoleSenseV2.ino` (note the matching folder + file name — Arduino IDE requirement) and Upload.

## Where to start contributing

- ~~**Task 4 (FFT)**~~ — done (this commit).
- **Task 5 (Storage)** — `storage.cpp`. Implement the slot serialization, CRC32 trailer, scan-on-load. Body layout is sketched in the file.
- **Task 6 (`/api/run-report`)** — `http_routes.cpp`. Compute cadence/GCT/pronation/L-R balance/flags from the FFT + outliers. FFT side is now working — query `fft_get_magnitude(channel, bin)` for the analysis.

Task 6 depends on Task 5 for persistence but can be developed independently against a single in-RAM run.
