# Profile / Auth System — Design Spec

> Status: implementation in progress (2026-05-09).

## Why

Today, anyone connected to the `SoleSense` WiFi AP can hit `POST /api/start` and record a run. There is no notion of "who" recorded it. Two practical problems:

1. **Privacy / fairness.** A stranger walking near the device can join the AP and overwrite an in-progress run.
2. **Body-weight calibration.** The FSR-jerk → BW/s loading-rate conversion currently assumes 70 kg. Real BW/s scales by `70 / actual_kg`. Without per-user body weight the number is a placeholder.

Both close with a per-user profile system stored on-device in NVS.

## Scope

**In scope (this iteration):**
- On-device user accounts: username + PIN, hashed and stored in NVS.
- Login → session token. Token in `Authorization: Bearer …` header for protected endpoints.
- Per-user `body_kg` field used in loading-rate calc.
- Frontend login screen, blocks the rest of the UI until logged in.
- "Owner" model: first registration is unrestricted; subsequent registrations require the existing owner's token (no walk-up account creation).

**Out of scope:**
- Multi-device sync — there's no internet, no cloud.
- Email / password reset — PIN reset is via USB-serial `factory_reset` command only.
- Multi-session — exactly one session-token slot in RAM at a time.
- Per-run history per user — added in a later iteration.

## Threat model

We are protecting against:
- A guest joining the AP and starting/stopping runs that aren't theirs.
- Someone reading the LittleFS data partition off a stolen device and recovering PINs (so we hash + salt).

We are NOT protecting against:
- Someone with USB access (they can re-flash firmware and bypass everything; physical access wins).
- Someone with prolonged WiFi access trying to brute-force a 4-digit PIN — we add a simple rate limit (3 wrong → 30 s lockout).

## Storage (NVS)

Namespace: `solesense_auth`. Per-user keys under a `users/<username>/` prefix.

| Key | Type | Notes |
|---|---|---|
| `users/<u>/pinhash` | bytes(32) | SHA-256(pin || salt) |
| `users/<u>/salt`    | bytes(16) | per-user random, generated at registration |
| `users/<u>/body_kg` | float    | for loading-rate BW/s calc |
| `users/<u>/created` | u64      | seconds-since-epoch (well, since-boot at registration) |
| `owner_user`        | string   | username of the device owner |
| `_init_done`        | u8       | sentinel; if missing, device is in claim-mode |

Estimated ~150 B per user. Plenty of room for ~50 users in the default NVS partition.

## Session model

One slot, RAM only:

```c
struct Session {
    bool      active;
    char      username[32];
    uint8_t   token[32];     // hex-encoded → 64 chars in HTTP header
    uint32_t  expires_ms;    // millis() rollover-aware
    float     body_kg;       // cached at login
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
| `GET`  | `/api/auth/state`                     | `{ ownerExists, sessionActive }` |
| `POST` | `/api/auth/register`                  | first call creates owner; subsequent calls require token |
| `POST` | `/api/auth/login`                     | `{ username, pin }` → `{ token, body_kg }` |

### Protected (require valid token)

| Method | Path | Notes |
|---|---|---|
| `POST` | `/api/auth/logout`        | clear session |
| `GET`  | `/api/auth/profile`       | current profile |
| `POST` | `/api/start`              | start recording |
| `POST` | `/api/stop`               | stop recording |
| `POST` | `/api/calibrate/zero`     | FSR zero |
| `POST` | `/api/calibrate/imu`      | IMU zero |
| `POST` | `/api/data/clear`         | wipe data |
| `POST` | `/api/sleep`              | deep sleep |

### Read-only diagnostic (intentionally still public)

`GET /api/sensor`, `GET /api/run-state`, `GET /api/run-report`, `GET /api/run-spectrum`, `GET /api/run-outliers`. These don't change device state and are useful for "is the device alive" probes.

## Failure modes

| Case | Response |
|---|---|
| Protected route, no `Authorization` header | `401 Unauthorized` |
| Token invalid or expired | `401 Unauthorized`, frontend kicks back to login |
| Login with wrong PIN, 3 times in a 60 s window | `429 Too Many Requests`, lock the username for 30 s |
| Register without owner-token after owner exists | `403 Forbidden` |
| NVS write failure | `500`, abort registration cleanly |

## Frontend changes

Login screen as the new first screen. Stored token in `localStorage` survives reload. On any 401 response, frontend wipes localStorage and bounces to login.

Loading-rate display reads `r.loadingRate` as before; the firmware now divides by the logged-in user's body weight, so the displayed BW/s is honest per-user.

## Implementation order

1. `auth.h/cpp` — NVS helpers, SHA-256, session struct, token generation.
2. New HTTP routes (`/api/auth/*`).
3. `require_auth()` middleware applied to protected routes.
4. Body-weight wiring into the loading-rate calc (replaces hardcoded 70 kg).
5. Frontend: login screen, localStorage token, Authorization header on every protected fetch.
6. Edge cases: PIN-attempt rate limit, 401-handler in frontend, factory-reset over USB serial.
