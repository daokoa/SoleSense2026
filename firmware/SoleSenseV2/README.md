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
| Goertzel FFT, 1024-sample window, 0.49 Hz bin width | `fft.h` / `fft.cpp` | ✅ done |
| Multi-slot ring-buffer flash storage | `storage.h` / `storage.cpp` | ✅ done |
| Time-domain step detector (any-zone OR-gate, 250 ms refractory, peak-relative release) | `state.cpp` | ✅ done |
| FSR + IMU sensor fusion (strike requires recent IMU impact when IMU is connected; falls back to FSR-only when not) | `state.cpp` + `SoleSenseV2.ino` | ✅ done |
| Ground-contact-time | `state.cpp` | ✅ done — heel-strike → toe-off interval, [50, 800] ms valid range |
| Total-pressure aggregate (peak `SUM(ch0..5)`) | `SoleSenseV2.ino` | ✅ done — exposed via `/api/run-report.maxTotalPressure` for diagnostics |
| HTTP routes (full set) | `http_routes.cpp` | ✅ done |
| FSR-jerk loading rate (BW/s) — uses logged-in user's body weight | `http_routes.cpp` + `SoleSenseV2.ino` | ✅ done |
| Cadence sanity clamp (60–240 spm) | `http_routes.cpp` | ✅ done |
| FSR-saturation injury flag | `http_routes.cpp` | ✅ done |
| **Auth / profile system** | `auth.h` / `auth.cpp` | ✅ done — NVS-backed users, SHA-256 + salt, owner-claim model, 30-min idle session, 3-failures-→30-s lockout |
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

1. **Body weight is per-user via the profile system.** Once an account is created with a `body_kg` value, the loading-rate BW/s conversion uses that. If no session is active (e.g., a non-protected `/api/run-report` call), the calc falls back to a 70 kg assumption.
2. **Loading-rate FSR saturation assumption.** The conversion assumes the FSR + voltage-divider hits ADC=4095 at exactly 10 kg of force. Actual saturation point depends on the divider resistor. The `fsr_saturated` flag in the run-report fires if peak total pressure suggests several channels hit the rail — telling the user "loading rate may be underreported." Per-divider calibration is the next refinement.
3. **IMU graceful fallback.** When the MPU-6050 is not soldered, `gImuConnected` stays `false` (zero stddev on accel_z) and the step detector skips IMU validation — so steps still count off the FSRs alone. Once the IMU is wired the validation auto-engages.
4. **Step detector debug logs are still on.** `step_detector_update()` in `state.cpp` prints `[Step] tentative` and `[Step] STEP/discard` to Serial whenever it fires. Useful for bring-up; strip for clean release builds.
5. **Hardware verification gap.** The full "30-second run with rhythmic foot strikes" end-to-end test has not been recorded yet.

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

## Auth / profile system

Spec: [`../../docs/superpowers/specs/2026-05-09-profile-system.md`](../../docs/superpowers/specs/2026-05-09-profile-system.md).

- Storage: NVS namespace `solesense_auth`, per-user keys under `u/<username>/`.
- Hashing: SHA-256(pin || 16-byte random salt) via mbedtls.
- One active session at a time, RAM-only, 30-minute idle timeout.
- "Owner claim" model: first registration is unrestricted, subsequent registrations require an existing-owner token.
- Rate limiter: 3 failed PINs in 60 s → 30 s lockout for that username.

Endpoints:

| Method | Path | Auth | Notes |
|---|---|---|---|
| `GET`  | `/api/auth/state`     | public  | `{ ownerExists, sessionActive, username }` |
| `POST` | `/api/auth/register`  | claim-mode → public; otherwise requires owner token | URL-encoded `username, pin, body_kg` |
| `POST` | `/api/auth/login`     | public  | URL-encoded `username, pin` → `{ ok, token, body_kg }` |
| `POST` | `/api/auth/logout`    | session | clear current session |
| `GET`  | `/api/auth/profile`   | session | current user info |

Protected endpoints (require `Authorization: Bearer <token>`):
`POST /api/start`, `POST /api/stop`, `POST /api/sleep`, `POST /api/calibrate/zero`, `POST /api/calibrate/imu`, `POST /api/data/clear`.

Public read-only endpoints (still available without a token, used for AP discovery and live diagnostics): `GET /api/device`, `GET /api/sensor`, `GET /api/run-state`, `GET /api/run-report`, `GET /api/run-spectrum`, `GET /api/run-outliers`, `GET /api/storage-state`.

To factory-reset the auth NVS, run `Preferences.clear()` over USB serial — there is no in-app reset path.

## Tasks for new contributors

In priority order:

1. **Per-FSR saturation calibration.** Apply a known weight (5 kg) and capture each channel's ADC reading. Use that to fit a per-channel `force_per_count` so loading rate isn't bottlenecked by the 4095=10 kg assumption.
2. **Hardware verification.** 30 s real-run test on a wired insole. Confirm cadence, pressure distribution, contact time, loading rate, and IMU validation all behave under real running.
3. **EMA-baseline step detector.** The fixed-threshold Schmitt trigger can drift with FSR baseline (sweat, temperature). The `heelMean`/`heelStddev` args are already plumbed; wire them into a slow EMA baseline.
4. **Strip Serial debug** once confidence is high.
5. **Auto-stop after silence.** If no FSR strike for >120 s during a recording, auto-`/api/stop` to catch "user forgot to stop" — currently a TODO.
