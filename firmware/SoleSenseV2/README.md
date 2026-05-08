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
| HTTP routes (v0.2 new) | `http_routes.cpp` | ✅ done — `/api/run-state`, `/api/run-spectrum`, `/api/run-outliers`, and `/api/run-report` all return real numbers from FFT + outliers + running stats |
| Per-channel running stats (Welford) | `stats.h/.cpp` | ✅ done — extracted into its own module; used by FFT (DC removal), outliers (sigma), and report (zone means / pronation) |
| Deep sleep | `SoleSenseV2.ino` | ✅ done |

## What flashing this gets you right now

If you uploaded this sketch to the XIAO instead of v0.1, it would:

- Boot, mount LittleFS, init sensors, bring up the SoleSense AP at 192.168.4.1
- Accept `/api/start` and `/api/stop`, run the 50 Hz sample loop, pause when no clients are on the AP, resume when they reconnect
- Compute online running-mean/stddev per channel, route samples through outlier detection vs. FFT
- Track up to 100 outliers per run with full history
- Return `/api/run-state` with live `recording`, `run_active`, `elapsed_ms`, `sample_count`, `clients_connected`, `outlier_count`

What it would **not** yet do:

All v0.2 firmware logic is now real (Tasks 1–6 done). What v0.2 does end-to-end:

- 50 Hz sampling of 6 FSRs + IMU; pause-on-disconnect via AP station count
- Outlier filter: top-100 buffer keyed on |sigma|; outliers excluded from FFT
- Goertzel FFT: 12 channels × 18 bins, DC-removed via Welford running mean, validated against a Python reference (0.0% error on-bin)
- Multi-slot ring buffer in flash: every 3 s a CRC-protected snapshot lands in one of 10 rotating slots; corrupted writes correctly rejected on load
- `/api/run-report` computes cadence, GCT, loading-rate, pronation, L/R balance, pressure distribution, and 7 injury flags from the FFT + outliers + stats — output shape matches what the dao UI's `render()` already consumes
- Diagnostic endpoints: `/api/fft-selftest`, `/api/storage-selftest`, `/api/storage-state`, `/api/run-state`, `/api/run-spectrum`, `/api/run-outliers`

What's still pending:
- **Task 7 (frontend rewire)** — dao UI currently records client-side, parses CSV, and runs the analysis in JS. Switch it to: poll `/api/run-state` for the timer, fetch `/api/run-report` after stop, drop the JS-side sample array entirely.
- Hardware verification on a real XIAO with sensors wired (no v0.2 firmware has been flashed yet — v0.1 still owns the demo).

## Compile / flash

The same Arduino IDE setup as v0.1. Open `firmware/SoleSenseV2/SoleSenseV2.ino` (note the matching folder + file name — Arduino IDE requirement) and Upload to flash the sketch.

For LittleFS data (the frontend), sync from `software/frontend/dao-v2/` then run the v0.2 flash script:

```bash
cp software/frontend/dao-v2/index.html firmware/SoleSenseV2/data/index.html
bash firmware/SoleSenseV2/flash-littlefs.sh
```

The script auto-detects mklittlefs/esptool/USB port — same logic as the v0.1 one, just pointed at the v0.2 sketch's `data/` folder. After flashing, verify v0.2 is running by hitting `http://192.168.4.1/api/device` and confirming the firmware field reports v0.2.

## Where to start contributing

- ~~**Task 4 (FFT)**~~ — done.
- ~~**Task 5 (Storage)**~~ — done.
- ~~**Task 6 (run-report)**~~ — done.
- **Task 7 (Frontend rewire)** — `software/frontend/dao/index.html`. Drop the JS-side sample array and CSV parsing; poll `/api/run-state` for the live timer, fetch `/api/run-report` after stop. Show a "Paused" pill when `run_state.run_active==false`. The response shape from `/api/run-report` matches what the existing `render(r)` function already consumes, so this is mostly removing JS, not adding.
- **Hardware verification** — flash v0.2 to a XIAO (use `firmware/SoleSenseV2/` instead of `firmware/SoleSense/`). Sensor wiring is unchanged from v0.1. Run a 30-second test, confirm `/api/run-report` returns sensible numbers, run `/api/storage-selftest` to verify crash recovery.
