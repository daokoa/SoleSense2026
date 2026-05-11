# Profile / Auth System -- Design Spec

> Status: implementation in progress (2026-05-09).

## Why

Today, anyone connected to the `SoleSense` WiFi AP can hit `POST /api/start` and record a run. There is no notion of "who" recorded it. Two practical problems:

1. **Privacy / fairness.** A stranger walking near the device can join the AP and overwrite an in-progress run.
2. **Body-weight calibration.** The FSR-jerk -> BW/s loading-rate conversion currently assumes 70 kg. Real BW/s scales by `70 / actual_kg`. Without per-user body weight the number is a placeholder.

Both close with a per-user profile system stored on-device in NVS.

## Scope

**In scope (this iteration):**
- On-device user accounts: username + PIN, hashed and stored in NVS.
- Login -> session token. Token in `Authorization: Bearer ...` header for protected endpoints.
- Per-user `body_kg` field used in loading-rate calc.
- Frontend login screen, blocks the rest of the UI until logged in.
- "Owner" model: first registration claims the device. Subsequent registrations are walk-up self-signup (the shared WiFi password is the access gate), capped by `MAX_USERS = 50` and a 3-per-60-s sliding-window rate limit.

**Out of scope:**
- Multi-device sync -- there's no internet, no cloud.
- Email / password reset -- PIN reset is via USB-serial `factory_reset` command only.
- Multi-session -- exactly one session-token slot in RAM at a time.
- Per-run history per user -- added in a later iteration.

## Threat model

We are protecting against:
- A guest joining the AP and starting/stopping runs that aren't theirs.
- Someone reading the LittleFS data partition off a stolen device and recovering PINs (so we hash + salt).

We are NOT protecting against:
- Someone with USB access (they can re-flash firmware and bypass everything; physical access wins).
- Someone with prolonged WiFi access trying to brute-force a 4-digit PIN -- we add a simple rate limit (3 wrong -> 30 s lockout).

## Storage (NVS)

Namespace: `solesense_auth`. Per-user data is stored as four flat keys, prefixed by a single-character type tag (`Preferences` library limits key length to 15 chars, so `<tag>_<username>` keeps everything in budget for a 13-char username).

| Key | Type | Notes |
|---|---|---|
| `s_<u>`             | bytes(16) | per-user random salt |
| `h_<u>`             | bytes(32) | SHA-256(pin || salt) |
| `w_<u>`             | float     | body_kg, for loading-rate BW/s calc |
| `t_<u>`             | float     | height_cm, for stride-length + estimated-speed derivation |
| `owner_user`        | string    | username of the device owner |
| `uc`                | u16       | total user count (incremented atomically on each register) |

About ~250 B per user with NVS overhead. The 24 KB default NVS partition fits 50 users comfortably (the cap is set by `MAX_USERS`, not by capacity).

## Session model

One slot, RAM only:

```c
struct Session {
    bool      active;
    char      username[32];
    char      token_hex[65];  // 32 random bytes hex-encoded + null
    uint32_t  expires_ms;     // millis() rollover-aware
    float     body_kg;        // cached at login
    float     height_cm;      // cached at login (Cavanagh & Williams stride math)
} gSession;
```

Token is 32 random bytes from `esp_random()`. Expiry default: 30 minutes after the last protected request (idle timeout, refreshed on each successful auth check).

Logout clears the slot. Re-login replaces the slot.

## API

### Public

| Method | Path | Notes |
|---|---|---|
| `GET`  | `/`                                   | static files |
| `GET`  | `/api/device`                         | identity, AP discovery |
| `GET`  | `/api/auth/state`                     | `{ ownerExists, sessionActive, username, userCount, maxUsers }` |
| `POST` | `/api/auth/register`                  | first call claims the device; subsequent calls are walk-up self-signup, capped by `MAX_USERS` + a 3-per-60-s rate limit |
| `POST` | `/api/auth/login`                     | `{ username, pin }` -> `{ token, body_kg, height_cm, username }` |

### Protected (require valid token)

| Method | Path | Notes |
|---|---|---|
| `POST` | `/api/auth/logout`        | clear session |
| `GET`  | `/api/auth/profile`       | current profile (`username`, `body_kg`, `height_cm`) |
| `POST` | `/api/auth/profile`       | edit `body_kg` and/or `height_cm` without re-registering |
| `POST` | `/api/start`              | start recording |
| `POST` | `/api/stop`               | stop recording |
| `POST` | `/api/calibrate/zero`     | FSR zero |
| `POST` | `/api/calibrate/imu`      | IMU zero |
| `POST` | `/api/data/clear`         | wipe the run-snapshot ring buffer |
| `POST` | `/api/sleep`              | deep sleep |

### Read-only diagnostic (intentionally still public)

`GET /api/sensor`, `GET /api/run-state`, `GET /api/run-report`, `GET /api/run-spectrum`, `GET /api/run-outliers`. These don't change device state and are useful for "is the device alive" probes.

## Failure modes

| Case | Response |
|---|---|
| Protected route, no `Authorization` header | `401 Unauthorized` |
| Token invalid or expired | `401 Unauthorized`, frontend kicks back to login |
| Login with wrong PIN, 3 times in a 60 s window | `429 Too Many Requests`, lock the username for 30 s |
| Register over the `MAX_USERS` cap | `507 Insufficient Storage` |
| Register more than 3 times in a 60 s window | `429 Too Many Requests` |
| NVS write failure | `400 Bad Request` (after atomic rollback of any partial state) |
| Register with invalid body_kg (< 25 or > 250 kg) | `400 Bad Request` |
| Register with invalid height_cm (< 100 or > 250 cm) | `400 Bad Request` |

## Frontend changes

Login screen as the new first screen. Stored token in `localStorage` survives reload. On any 401 response, frontend wipes localStorage and bounces to login.

Loading-rate display reads `r.loadingRate` as before; the firmware now divides by the logged-in user's body weight, so the displayed BW/s is honest per-user.

## Implementation order

1. `auth.h/cpp` -- NVS helpers, SHA-256, session struct, token generation.
2. New HTTP routes (`/api/auth/*`).
3. `require_auth()` middleware applied to protected routes.
4. Body-weight wiring into the loading-rate calc (replaces hardcoded 70 kg).
5. Frontend: login screen, localStorage token, Authorization header on every protected fetch.
6. Edge cases: PIN-attempt rate limit, 401-handler in frontend, factory-reset over USB serial.
