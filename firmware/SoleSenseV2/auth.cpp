// =============================================================================
// SoleSense v0.2 — auth.cpp
// =============================================================================
#include "auth.h"

#include <Preferences.h>
#include <mbedtls/sha256.h>
#include <esp_random.h>

Session gSession;

static Preferences sNvs;
static constexpr const char* NS = "solesense_auth";

// ── In-memory rate limiter ──────────────────────────────────────────────────
// Per-username, slot-allocated. Plenty for a single-insole device.
struct FailEntry {
  char     user[32];
  uint8_t  count;
  uint32_t window_start_ms;
  uint32_t lockout_until_ms;
};
static constexpr uint8_t MAX_FAIL_SLOTS = 8;
static FailEntry sFails[MAX_FAIL_SLOTS] = {};

static FailEntry* find_or_alloc_fail(const String& user) {
  // Look for existing slot.
  for (auto& e : sFails) {
    if (strncmp(e.user, user.c_str(), sizeof(e.user)) == 0) return &e;
  }
  // Take the oldest unused / least recently failed slot.
  FailEntry* oldest = &sFails[0];
  for (auto& e : sFails) {
    if (e.user[0] == 0) { oldest = &e; break; }
    if (e.window_start_ms < oldest->window_start_ms) oldest = &e;
  }
  memset(oldest, 0, sizeof(*oldest));
  strncpy(oldest->user, user.c_str(), sizeof(oldest->user) - 1);
  return oldest;
}

// ── SHA-256 helper ───────────────────────────────────────────────────────────
static void sha256_concat(const uint8_t* a, size_t alen,
                          const uint8_t* b, size_t blen,
                          uint8_t out[32]) {
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);
  mbedtls_sha256_update(&ctx, a, alen);
  mbedtls_sha256_update(&ctx, b, blen);
  mbedtls_sha256_finish(&ctx, out);
  mbedtls_sha256_free(&ctx);
}

static void random_bytes(uint8_t* buf, size_t n) {
  for (size_t i = 0; i < n; i += 4) {
    uint32_t r = esp_random();
    size_t take = (n - i) < 4 ? (n - i) : 4;
    memcpy(buf + i, &r, take);
  }
}

static void hex_encode(const uint8_t* in, size_t n, char* out) {
  static const char* H = "0123456789abcdef";
  for (size_t i = 0; i < n; i++) {
    out[i*2]     = H[(in[i] >> 4) & 0xF];
    out[i*2 + 1] = H[in[i] & 0xF];
  }
  out[n*2] = 0;
}

// NVS key names are limited to 15 characters. Use a 2-char prefix per field
// + the username, giving us 13 chars for the username. Validation in
// auth_register enforces this. Prefixes are lowercase letters; the global
// "owner_user" key is distinct and won't collide.
static String key_for(const char* user, char prefix) {
  String s;
  s.reserve(15);
  s += prefix;
  s += '_';
  s += user;
  return s;
}

// Allowed username characters: letters, digits, underscore. Length 3..13.
static bool valid_username(const String& u) {
  if (u.length() < 3 || u.length() > 13) return false;
  for (size_t i = 0; i < u.length(); i++) {
    char c = u[i];
    bool ok = (c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              c == '_';
    if (!ok) return false;
  }
  return true;
}

// ── Public API ───────────────────────────────────────────────────────────────
void auth_init() {
  sNvs.begin(NS, false);
  // Nothing else to bootstrap — owner detection happens lazily.
  Serial.printf("[Auth] init; ownerExists=%s\n",
                auth_owner_exists() ? "yes" : "no");
}

bool auth_owner_exists() {
  return sNvs.isKey("owner_user");
}

bool auth_in_claim_mode() {
  return !auth_owner_exists();
}

int auth_register(const String& username, const String& pin, float body_kg) {
  if (!valid_username(username)) return -3;
  if (pin.length() < 4 || pin.length() > 16) return -3;
  if (body_kg < 25.0f || body_kg > 250.0f) return -3;

  // Username taken? Check with isKey on the salt key (matches the type we wrote).
  String saltKey = key_for(username.c_str(), 's');
  if (sNvs.isKey(saltKey.c_str())) return -1;

  uint8_t salt[16];
  random_bytes(salt, sizeof(salt));
  uint8_t hash[32];
  sha256_concat((const uint8_t*)pin.c_str(), pin.length(), salt, sizeof(salt), hash);

  size_t w1 = sNvs.putBytes(saltKey.c_str(),                        salt, sizeof(salt));
  size_t w2 = sNvs.putBytes(key_for(username.c_str(), 'h').c_str(), hash, sizeof(hash));
  size_t w3 = sNvs.putFloat(key_for(username.c_str(), 'w').c_str(), body_kg);
  if (w1 != sizeof(salt) || w2 != sizeof(hash) || w3 == 0) {
    Serial.printf("[Auth] register NVS write failed: w1=%u w2=%u w3=%u\n",
                  (unsigned)w1, (unsigned)w2, (unsigned)w3);
    return -2;
  }

  if (auth_in_claim_mode()) {
    sNvs.putString("owner_user", username);
  }

  // Auto-grant session for the freshly registered user.
  return auth_login(username, pin);
}

int auth_login(const String& username, const String& pin) {
  // Rate limit
  {
    FailEntry* fe = find_or_alloc_fail(username);
    if (fe->lockout_until_ms != 0 && (int32_t)(millis() - fe->lockout_until_ms) < 0) {
      return -3;
    }
  }

  if (!valid_username(username)) return -1;

  String saltKey = key_for(username.c_str(), 's');
  if (!sNvs.isKey(saltKey.c_str())) {
    auth_record_failure_and_check_lockout(username);
    return -1;
  }
  uint8_t salt[16];
  size_t saltLen = sNvs.getBytes(saltKey.c_str(), salt, sizeof(salt));
  if (saltLen != sizeof(salt)) return -1;

  uint8_t storedHash[32];
  size_t hashLen = sNvs.getBytes(key_for(username.c_str(), 'h').c_str(),
                                 storedHash, sizeof(storedHash));
  if (hashLen != sizeof(storedHash)) return -1;

  uint8_t computed[32];
  sha256_concat((const uint8_t*)pin.c_str(), pin.length(), salt, sizeof(salt), computed);

  if (memcmp(storedHash, computed, sizeof(computed)) != 0) {
    auth_record_failure_and_check_lockout(username);
    return -2;
  }

  // Build session
  uint8_t tok[32];
  random_bytes(tok, sizeof(tok));
  gSession.active = true;
  strncpy(gSession.username, username.c_str(), sizeof(gSession.username) - 1);
  gSession.username[sizeof(gSession.username) - 1] = 0;
  hex_encode(tok, sizeof(tok), gSession.token_hex);
  gSession.expires_ms = millis() + SESSION_IDLE_MS;
  gSession.body_kg    = sNvs.getFloat(key_for(username.c_str(), 'w').c_str(), 70.0f);

  // Reset failure count for this user
  for (auto& e : sFails) {
    if (strncmp(e.user, username.c_str(), sizeof(e.user)) == 0) {
      memset(&e, 0, sizeof(e));
    }
  }

  Serial.printf("[Auth] login ok user=%s body_kg=%.1f\n",
                gSession.username, gSession.body_kg);
  return 0;
}

void auth_logout() {
  if (gSession.active) {
    Serial.printf("[Auth] logout user=%s\n", gSession.username);
  }
  memset(&gSession, 0, sizeof(gSession));
}

bool auth_check_token(const String& token) {
  if (!gSession.active) return false;
  if (token.length() != 64) return false;
  if (strncmp(token.c_str(), gSession.token_hex, 64) != 0) return false;
  if ((int32_t)(millis() - gSession.expires_ms) >= 0) {
    Serial.printf("[Auth] session expired user=%s\n", gSession.username);
    auth_logout();
    return false;
  }
  // Push deadline forward
  gSession.expires_ms = millis() + SESSION_IDLE_MS;
  return true;
}

String auth_extract_bearer(const String& header) {
  if (!header.startsWith("Bearer ")) return "";
  return header.substring(7);
}

void auth_factory_reset() {
  Serial.println("[Auth] FACTORY RESET — wiping all profiles");
  sNvs.clear();
  memset(&gSession, 0, sizeof(gSession));
  memset(sFails, 0, sizeof(sFails));
  Serial.println("[Auth] reset complete; device is back in claim mode");
}

void auth_serial_console_tick() {
  // Accumulate a line buffer; act on each newline.
  static char  line[64];
  static uint8_t pos = 0;
  while (Serial.available()) {
    int c = Serial.read();
    if (c < 0) break;
    if (c == '\r') continue;
    if (c == '\n') {
      line[pos] = 0;
      if (strcmp(line, "factory_reset") == 0) {
        auth_factory_reset();
      } else if (pos > 0) {
        Serial.printf("[Console] unknown command: %s\n", line);
        Serial.println("  available: factory_reset");
      }
      pos = 0;
    } else if (pos < sizeof(line) - 1) {
      line[pos++] = (char)c;
    }
  }
}

bool auth_record_failure_and_check_lockout(const String& username) {
  FailEntry* fe = find_or_alloc_fail(username);
  uint32_t now = millis();
  // Expire window after 60 s
  if (now - fe->window_start_ms > 60000UL) {
    fe->window_start_ms = now;
    fe->count = 0;
  }
  fe->count++;
  if (fe->count >= 3) {
    fe->lockout_until_ms = now + 30000UL;   // 30 s lockout
    Serial.printf("[Auth] lockout user=%s for 30 s\n", username.c_str());
    return true;
  }
  return false;
}
