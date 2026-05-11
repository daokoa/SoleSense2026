# SoleSense Firmware v0.2

The post-demo architecture: all run state lives on the MCU, no browser-side analysis, crash-recoverable flash storage. Now sampling at **500 Hz** with a **3-zone × medial/lateral sensor layout** (Choi 2024 with FSR E moved next to the heel).

> v0.1 still lives at [`../SoleSense/`](../SoleSense/) for the live demo.
> v0.2 has now reached feature-parity on the metrics that matter (step count, cadence, ground-contact-time, loading rate). It is the path forward.

## Background

- **Architecture spec:** [`../../docs/superpowers/specs/2026-05-06-v0.2-data-architecture.md`](../../docs/superpowers/specs/2026-05-06-v0.2-data-architecture.md)
- **Implementation plan:** [`../../docs/superpowers/plans/2026-05-06-v0.2-firmware.md`](../../docs/superpowers/plans/2026-05-06-v0.2-firmware.md)
- **Frontend (v0.2-aware viewer):** [`../../software/frontend/solesense-v2/index.html`](../../software/frontend/solesense-v2/index.html)

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
cp software/frontend/solesense-v2/index.html firmware/SoleSenseV2/data/index.html
bash firmware/SoleSenseV2/flash-littlefs.sh
```

After flashing, verify:

```bash
curl --noproxy '*' -s http://192.168.4.1/api/device | python3 -m json.tool | head -3
```

`firmware` should read `SoleSense v0.2-dev` and `sampleRateHz` should read `500`.

## Security & resilience model

What's protected, what's not, and how the firmware survives the obvious failure modes.

### Data at rest

| Data | Where | Integrity | Confidentiality |
|---|---|---|---|
| User PINs | NVS namespace `solesense_auth` (`h_<user>` keys) | NVS provides per-page CRC + wear-leveling. Atomic on partial writes — a power loss mid-write either keeps the old value or rolls back, never half-applied. | SHA-256 with a 16-byte per-user random salt. Pulling the flash chip and dumping NVS reveals only `(salt, hash)` pairs — recovering the PIN requires brute-forcing the hash, which on a 4-digit PIN is fast unless rate-limited at the auth layer (which it is, see below). |
| Run data | LittleFS `/run.bin`, 10-slot ring buffer | Each slot is wrapped with a magic header (`0x55EAB001`) + CRC32 + magic trailer (`0xC0DEF00D`). On boot/reconnect we scan all 10 slots and pick the newest with a valid trailer. A power loss mid-flush corrupts at most one slot; the previous valid slot is loaded transparently. | Not encrypted. Anyone with physical USB can read the recorded run. |
| Calibration offsets (FSR / IMU) | NVS `solesense_main` | NVS-protected | Plaintext (no PII) |
| Body weight | NVS `solesense_auth` (`w_<user>`) | NVS-protected | Plaintext within NVS — see PIN row for what that means |

### Auth attacks

| Attack | Defense |
|---|---|
| **PIN brute-force over the AP** | 3 wrong PINs in any 60-second window for the same username → 30-second lockout (in-RAM, per-username, see `auth.cpp::auth_record_failure_and_check_lockout`). |
| **Session-token forgery** | 32-byte token from `esp_random()` (hardware TRNG). 256 bits of entropy. Compared with `memcmp` (constant-time enough — the token's a one-shot, not an HMAC). |
| **Token replay after expiry** | Every successful auth check compares `expires_ms` against `millis()` and drops the session if past. Idle timeout: 30 minutes. |
| **Token replay after logout** | Logout clears the slot in RAM. Subsequent requests with that token get 401. |
| **Walk-up account creation** | Only the first `/api/auth/register` is unrestricted (claim mode). After that, registration requires the existing owner's token. |
| **Physical USB attacker** | Out of scope. They can re-flash firmware; nothing in software stops that. The `factory_reset` USB-serial command is intentionally available so a legitimate device owner can recover from a forgotten PIN. |
| **DoS via login flood** | Per-username lockout limits cost. No global rate limiter today; if the device is exposed to a hostile network for long periods, add one in `auth.cpp`. |

### Run-time edge cases

| Edge case | Behaviour |
|---|---|
| **WiFi client disconnects mid-run** | `state.cpp` flips `gRunActive=false`, freezes the elapsed-time counter, keeps the last sample-loop state in RAM. Reconnect → counter resumes. The frontend's `pollRunState` shows a "Paused" badge while disconnected. |
| **Phone leaves AP entirely (out of range)** | Same as above on the device side. The frontend's `/api/run-state` polls fail; after 3 consecutive failures the recording screen shows a `Device unreachable — reconnect to SoleSense WiFi` banner instead of fabricating timer values. |
| **Power loss mid-recording** | Last valid CRC-protected slot in `/run.bin` is the source of truth on next boot. At most ~3 seconds of post-flush data is lost (the inter-flush interval). |
| **User hits Stop with bad WiFi** | Frontend retries `/api/stop` 4 times with backoff (`authPostRetry`). 409 ("not recording") is treated as success. After failures, the local UI advances to the report anyway — `/api/run-report` will succeed once the device is reachable again. |
| **AI Coach with no internet** | Frontend's `runAiAnalysis()` catches the fetch failure and renders a friendly message ("Couldn't reach the AI coach…") under the Coach panel; the rule-based flags above remain authoritative. |
| **AI Coach with internet but worker quota hit** | Worker returns 502 with a structured error; frontend surfaces the message in the same error banner. |
| **`/api/start` while already recording** | 409. Frontend doesn't depend on this for correctness. |
| **`/api/stop` while idle** | 409. Treated as success by `authPostRetry`. |
| **Sleep request while recording** | 409 — `gSleepRequested` is only honoured from idle. |
| **NVS write failure during register** | `auth_register` returns -2 with a `[Auth] register NVS write failed: w1=… w2=… w3=…` log line. Frontend shows "nvs error". User retries (transient) or runs `factory_reset` over USB serial. |
| **Concurrent registers** | Single-core MCU, AsyncTCP serializes route handlers — only one register runs at a time. |
| **Browser cache after LittleFS reflash** | Documented gotcha. Hard-refresh required. The firmware version + sample rate in `/api/device` are the easiest way to confirm what's actually running. |

### Frontend-only resilience

- `localStorage` corruption → next protected fetch returns 401 → `authFetch` wipes localStorage and bounces to login.
- The token is never logged or written to a query string.
- The Cloudflare Worker URL is the only outbound network call; if blocked (corporate WiFi, captive portal), the AI Coach panel falls back to its error state without breaking the rest of the report.

### Out of scope

- **Encrypted run data at rest.** Adding AES-CTR with a key derived from the owner's PIN would protect against flash-chip extraction, but bricks the device on PIN loss. Not worth it for our use case.
- **Per-device certificates / TLS on the AP.** SoleSense serves plaintext HTTP over its own AP. Anyone on the AP can sniff the JSON. Defended by: it's the user's own AP, password-gated WiFi (`solesense`), and there's no PII richer than body weight on the wire.
- **Audit log of state transitions.** Recording start/stop happen, but there's no per-user history of past runs persisted across reboots. Once the IMU lands and the run-history feature ships, this becomes worth doing.

## Auth / profile system

Spec: [`../../docs/superpowers/specs/2026-05-09-profile-system.md`](../../docs/superpowers/specs/2026-05-09-profile-system.md).

- Storage: NVS namespace `solesense_auth`, per-user keys `s_<user>` (16-byte salt), `h_<user>` (32-byte SHA-256 hash), `w_<user>` (float body_kg). Plus globals `owner_user` (string) and `uc` (uint16 user count).
- Hashing: SHA-256(pin || 16-byte random salt) via mbedtls.
- One active session at a time, RAM-only, 30-minute idle timeout.
- **Self-signup** — any client on the SoleSense AP can create an account. The first registration becomes the "owner" (a label, not a role); subsequent accounts are equal peers.
- **Atomic registration** — writes salt → hash → body_kg in sequence, rolling back partial state on any NVS failure. A username slot is never left half-written.
- **Anti-abuse caps**:
  - **`MAX_USERS = 20`** total accounts. Further registrations return HTTP `507 Insufficient Storage` with message *"This device is full (account limit reached)."*
  - **Registration rate limit** = `REG_RATE_MAX_PER_WINDOW (3) / REG_RATE_WINDOW_MS (60 s)` global sliding window. 4th signup within 60 s returns HTTP `429`.
- Login PIN rate limiter: 3 failed PINs in 60 s → 30 s lockout for that username.

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
