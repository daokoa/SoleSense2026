// =============================================================================
// SoleSense v0.2 — auth.h
// On-device user/profile/auth system. Spec:
//   docs/superpowers/specs/2026-05-09-profile-system.md
//
// One-session, single-slot model. Sessions are RAM-only (no persistence
// across reboot — re-login required). User profiles (PIN hash + body weight)
// live in NVS under namespace "solesense_auth".
// =============================================================================
#pragma once

#include <Arduino.h>

// One active session at a time. Cleared on logout, on idle expiry, or when
// /api/auth/login replaces it.
struct Session {
  bool      active        = false;
  char      username[32]  = {0};
  char      token_hex[65] = {0};   // 32 bytes → 64 hex chars + null
  uint32_t  expires_ms    = 0;
  float     body_kg       = 70.0f;
};
extern Session gSession;

// Whether the device has an owner (first user) yet.
bool auth_owner_exists();

// Initialise NVS namespace, load any persisted state. Call from setup().
void auth_init();

// Register a new user. If no owner exists, creates the owner and grants a
// session immediately. If an owner exists, the caller must already hold a
// valid session — enforced by the route handler, not here.
//
// Returns 0 on success, negative on error:
//   -1 username taken
//   -2 NVS write failure
//   -3 invalid input (empty PIN, etc.)
int auth_register(const String& username, const String& pin, float body_kg);

// Log in an existing user. Returns 0 on success, negative on error:
//   -1 unknown username
//   -2 wrong PIN
//   -3 rate-limited (too many recent failures)
int auth_login(const String& username, const String& pin);

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
