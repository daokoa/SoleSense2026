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
| Multi-slot ring buffer storage | `storage.h/.cpp` | ✅ done — per-slot files with CRC32+magic trailer; corrupted writes correctly rejected by loader. Validated end-to-end via `POST /api/storage-selftest`. |
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

- Compute injury flags for `/api/run-report` (returns placeholders — Task 6)

What it WILL now do (Tasks 4 + 5 landed):

- Compute correctly-scaled FFT magnitudes per channel/bin every 256 samples (~5 s at 50 Hz)
- Expose `/api/run-spectrum` with real numbers per (channel, bin)
- Persist a snapshot of FFT magnitudes + outliers every 3 s into a CRC-protected ring of 10 flash slots
- Recover the most-recent valid snapshot on reboot or via `/api/storage-state`
- Expose `/api/fft-selftest` and `/api/storage-selftest` for on-bench validation

## Compile / flash

The same Arduino IDE setup as v0.1. Open `firmware/SoleSenseV2/SoleSenseV2.ino` (note the matching folder + file name — Arduino IDE requirement) and Upload.

## Where to start contributing

- ~~**Task 4 (FFT)**~~ — done.
- ~~**Task 5 (Storage)**~~ — done.
- **Task 6 (`/api/run-report`)** — `http_routes.cpp:handle_run_report`. Compute cadence / GCT / loading-rate / pronation / L-R balance / pressure-distribution / injury flags from the FFT bins + outlier buffer. The data sources are all live now: `fft_get_magnitude(c, b)`, `outliers_at(i)`, `outliers_count()`, `gRunElapsedMs`. Use the same flag thresholds as `software/frontend/dao/index.html`'s old in-browser analysis (`THRESH={cadence_low:160, contact_high:300, loading_high:60, pronate_high:15, supinate_low:-8, asym_high:10}`).
- **Task 7 (Frontend rewire)** — `software/frontend/dao/index.html`. Drop the JS-side sample array and CSV parsing; poll `/api/run-state` for the live timer, `/api/run-spectrum` if you want a live waveform display, `/api/run-report` after stop. Show a "Paused" pill when `run_state.run_active==false`.

Tasks 6 and 7 are independent. Task 6 is the bigger logic piece.
