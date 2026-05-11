// On-device auth: one RAM-only session at a time; user profiles persisted
// in NVS namespace "solesense_auth". Full spec:
// docs/design/specs/2026-05-09-profile-system.md
#pragma once

#include <Arduino.h>

// One active session at a time. Cleared on logout, on idle expiry, or when
// /api/auth/login replaces it.
struct Session {
  bool      active        = false;
  char      username[32]  = {0};
  char      token_hex[65] = {0};   // 32 bytes -> 64 hex chars + null
  uint32_t  expires_ms    = 0;
  float     body_kg       = 70.0f;
  float     height_cm     = 170.0f;
};
extern Session gSession;

// Whether the device has an owner (first user) yet.
bool auth_owner_exists();

// Total number of registered users on this device. Bounded by MAX_USERS;
// once that ceiling is hit, further registrations return -6 (full).
uint16_t auth_user_count();

// Hard caps surface to callers (route handlers, README, frontend) so they
// can display accurate "X of Y accounts used" or reject early.
static constexpr uint16_t MAX_USERS               = 50;     // total accounts (NVS budget ~24 KB; each user ~250 B with overhead)
static constexpr uint32_t REG_RATE_WINDOW_MS      = 60000;  // 60-second window
static constexpr uint8_t  REG_RATE_MAX_PER_WINDOW = 3;      // <=3 signups / window

// Initialise NVS namespace, load any persisted state. Call from setup().
void auth_init();

// Register a new user. If no owner exists, creates the owner and grants a
// session immediately. If an owner exists, the caller must already hold a
// valid session -- enforced by the route handler, not here.
//
// Returns 0 on success, negative on error:
//   -1 username taken
//   -2 NVS write failure
//   -3 invalid input (empty PIN, etc.)
int auth_register(const String& username, const String& pin, float body_kg, float height_cm);

// Log in an existing user. Returns 0 on success, negative on error:
//   -1 unknown username
//   -2 wrong PIN
//   -3 rate-limited (too many recent failures)
int auth_login(const String& username, const String& pin);

// Update body_kg and/or height_cm for the currently signed-in user.
// Pass NaN for any field you don't want to change. Returns 0 on success,
// negative on validation failure. Persists to NVS and updates gSession
// atomically (NVS first, then gSession), so a failed write doesn't leave
// the in-RAM session out of sync with flash.
int auth_update_profile(float body_kg, float height_cm);

// Drop the active session.
void auth_logout();

// Validate the Authorization header value (whatever follows "Bearer "). Sets
// gSession.expires_ms forward by SESSION_IDLE_MS on success. Returns true if
// the header matches the active session token AND the session hasn't expired.
bool auth_check_token(const String& token);

// Convenience: extract token from a full "Bearer <token>" header.
String auth_extract_bearer(const String& header);

// Idle timeout. Each successful auth_check_token() pushes the deadline
// forward by this much; once exceeded, the session is dropped.
static constexpr uint32_t SESSION_IDLE_MS = 30UL * 60UL * 1000UL;   // 30 min

// Log a failed login attempt for a username. Used by the rate limiter.
// Returns true if this attempt should be REJECTED outright (locked out).
bool auth_record_failure_and_check_lockout(const String& username);

// One-time owner-claim check. Used by the register handler to decide if it
// should require an existing-owner token.
bool auth_in_claim_mode();

// Wipe the entire auth NVS namespace and drop the active session. After
// this returns the device is back in claim-mode. Triggered by the
// "factory_reset" USB-serial command -- there is no in-app reset path on
// purpose, since that would defeat the security model.
void auth_factory_reset();

// Drain Serial input and act on any "factory_reset\n" line. Call once per
// loop() iteration.
void auth_serial_console_tick();
