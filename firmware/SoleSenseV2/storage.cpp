// =============================================================================
// SoleSense v0.2 -- storage.cpp
//
// Multi-slot ring buffer in LittleFS. One small file per slot
// (/run_slot_0.bin .. /run_slot_9.bin), each containing
//   [SlotHeader][body of FFT magnitudes + outliers + reserved meta][SlotTrailer]
//
// On every save we round-robin to the next slot file. The trailer carries a
// CRC32 over header+body and a magic byte; both must match on load. A
// power-cut mid-write leaves a slot whose trailer is missing or partial, so
// load() filters it out and falls back to the previous slot.
//
// Self-test: storage_self_test() writes 3 slots, deliberately corrupts the
// newest, verifies load_latest() returns the second-newest. Trigger via
// POST /api/storage-selftest. Validated end-to-end in this commit.
// =============================================================================

#include "storage.h"
#include "fft.h"
#include "outliers.h"
#include "config.h"

#include <LittleFS.h>
#include <string.h>

// -- Body layout --------------------------------------------------------------
// [ FFT magnitudes ][ outlier_count u32 ][ Outlier[OUTLIER_CAPACITY] ][ 32 bytes reserved ]
static constexpr size_t SS_FFT_BYTES      = (size_t)N_CHANNELS_TOTAL * FFT_BINS_PER_CHAN * sizeof(float);
static constexpr size_t SS_OUTLIER_BYTES  = (size_t)OUTLIER_CAPACITY * sizeof(Outlier);
static constexpr size_t SS_BODY_BYTES     = SS_FFT_BYTES + sizeof(uint32_t) + SS_OUTLIER_BYTES + 32;
static constexpr size_t SS_TOTAL_BYTES    = sizeof(SlotHeader) + SS_BODY_BYTES + sizeof(SlotTrailer);

// Single static buffer used for both write and read. ~3 KB.
static uint8_t sBuf[SS_TOTAL_BYTES];

// Round-robin index + monotonic sequence.
static uint8_t  sNextSlot = 0;
static uint32_t sSlotSeq  = 1;

// -- Helpers ------------------------------------------------------------------

static String slot_path(uint8_t i) {
  return String("/run_slot_") + i + ".bin";
}

// IEEE 802.3 CRC-32. Self-contained so we don't depend on a platform header.
static uint32_t crc32_compute(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      uint32_t mask = -(int32_t)(crc & 1u);
      crc = (crc >> 1) ^ (0xEDB88320u & mask);
    }
  }
  return ~crc;
}

// Pack the body into dst (must have at least SS_BODY_BYTES of room).
static void serialise_body(uint8_t* dst) {
  size_t off = 0;

  // 1. FFT magnitudes (N_CHANNELS_TOTAL x FFT_BINS_PER_CHAN floats)
  for (uint8_t c = 0; c < N_CHANNELS_TOTAL; c++) {
    for (uint8_t b = 0; b < FFT_BINS_PER_CHAN; b++) {
      float m = fft_get_magnitude(c, b);
      memcpy(dst + off, &m, sizeof(float));
      off += sizeof(float);
    }
  }

  // 2. Outlier count (4 bytes for alignment)
  uint32_t n = outliers_count();
  memcpy(dst + off, &n, sizeof(uint32_t));
  off += sizeof(uint32_t);

  // 3. Outliers (always serialise the full capacity; unused entries zeroed)
  for (uint8_t i = 0; i < OUTLIER_CAPACITY; i++) {
    if (i < n) {
      const Outlier& o = outliers_at(i);
      memcpy(dst + off, &o, sizeof(Outlier));
    } else {
      memset(dst + off, 0, sizeof(Outlier));
    }
    off += sizeof(Outlier);
  }

  // 4. Reserved meta (32 bytes for forward compat)
  memset(dst + off, 0, 32);
  off += 32;

  // Sanity check
  if (off != SS_BODY_BYTES) {
    Serial.printf("[Storage] body serialise size mismatch: %u != %u\n",
                  (unsigned)off, (unsigned)SS_BODY_BYTES);
  }
}

// Write a complete slot to /run_slot_N.bin. Returns true on success.
static bool write_slot_at(uint8_t slot, uint32_t elapsed_ms, uint32_t sample_count, uint32_t run_done) {
  SlotHeader hdr;
  hdr.magic        = STORAGE_MAGIC_HEADER;
  hdr.slot_n       = sSlotSeq;
  hdr.timestamp_ms = elapsed_ms;
  hdr.sample_count = sample_count;
  hdr.run_done     = run_done;
  hdr.body_bytes   = SS_BODY_BYTES;

  memcpy(sBuf, &hdr, sizeof(SlotHeader));
  serialise_body(sBuf + sizeof(SlotHeader));

  uint32_t crc = crc32_compute(sBuf, sizeof(SlotHeader) + SS_BODY_BYTES);

  SlotTrailer tr;
  tr.crc32 = crc;
  tr.magic = STORAGE_MAGIC_TRAILER;
  memcpy(sBuf + sizeof(SlotHeader) + SS_BODY_BYTES, &tr, sizeof(SlotTrailer));

  String path = slot_path(slot);
  File f = LittleFS.open(path.c_str(), "w");
  if (!f) {
    Serial.printf("[Storage] open(%s) FAILED\n", path.c_str());
    return false;
  }
  size_t written = f.write(sBuf, SS_TOTAL_BYTES);
  f.close();
  if (written != SS_TOTAL_BYTES) {
    Serial.printf("[Storage] short write on slot %u: %u/%u\n",
                  slot, (unsigned)written, (unsigned)SS_TOTAL_BYTES);
    return false;
  }
  return true;
}

// Read + validate a slot. Returns true if the slot has a valid header,
// trailer, and matching CRC. On success, hdr is populated.
static bool read_and_validate_slot(uint8_t slot, SlotHeader& hdr) {
  String path = slot_path(slot);
  if (!LittleFS.exists(path.c_str())) return false;

  File f = LittleFS.open(path.c_str(), "r");
  if (!f) return false;
  if (f.size() < SS_TOTAL_BYTES) { f.close(); return false; }
  size_t got = f.read(sBuf, SS_TOTAL_BYTES);
  f.close();
  if (got != SS_TOTAL_BYTES) return false;

  memcpy(&hdr, sBuf, sizeof(SlotHeader));
  if (hdr.magic != STORAGE_MAGIC_HEADER) return false;
  if (hdr.body_bytes != SS_BODY_BYTES)   return false;

  SlotTrailer tr;
  memcpy(&tr, sBuf + sizeof(SlotHeader) + SS_BODY_BYTES, sizeof(SlotTrailer));
  if (tr.magic != STORAGE_MAGIC_TRAILER) return false;

  uint32_t crc = crc32_compute(sBuf, sizeof(SlotHeader) + SS_BODY_BYTES);
  if (crc != tr.crc32) return false;

  return true;
}

// -- Public API ---------------------------------------------------------------

void storage_init() {
  uint8_t exist = 0;
  for (uint8_t i = 0; i < STORAGE_SLOT_COUNT; i++) {
    if (LittleFS.exists(slot_path(i).c_str())) exist++;
  }
  Serial.printf("[Storage] init: %u/%u slot files present, %u bytes/slot\n",
                exist, (unsigned)STORAGE_SLOT_COUNT, (unsigned)SS_TOTAL_BYTES);

  // Recover the next-slot pointer if there's existing data.
  SlotHeader hdr;
  if (storage_load_latest(hdr)) {
    sSlotSeq = hdr.slot_n + 1;                  // ensure new writes get higher seq
    sNextSlot = (hdr.slot_n + 1) % STORAGE_SLOT_COUNT;
    Serial.printf("[Storage] resumed from slot_n=%lu @ %lu ms; next slot = %u\n",
                  (unsigned long)hdr.slot_n, (unsigned long)hdr.timestamp_ms, sNextSlot);
  }
}

void storage_begin_run() {
  sNextSlot = 0;
  sSlotSeq  = 1;
  Serial.println("[Storage] run begin (ring reset)");
}

void storage_save_snapshot(uint32_t elapsed_ms, uint32_t sample_count) {
  if (write_slot_at(sNextSlot, elapsed_ms, sample_count, /*run_done=*/0)) {
    Serial.printf("[Storage] flush slot %u (seq=%lu, ts=%lu ms)\n",
                  sNextSlot, (unsigned long)sSlotSeq, (unsigned long)elapsed_ms);
    sNextSlot = (sNextSlot + 1) % STORAGE_SLOT_COUNT;
    sSlotSeq++;
  }
}

void storage_end_run(uint32_t elapsed_ms, uint32_t sample_count) {
  if (write_slot_at(sNextSlot, elapsed_ms, sample_count, /*run_done=*/1)) {
    Serial.printf("[Storage] final slot %u (seq=%lu, ts=%lu ms, run_done=1)\n",
                  sNextSlot, (unsigned long)sSlotSeq, (unsigned long)elapsed_ms);
    sNextSlot = (sNextSlot + 1) % STORAGE_SLOT_COUNT;
    sSlotSeq++;
  }
}

void storage_clear() {
  uint8_t removed = 0;
  for (uint8_t i = 0; i < STORAGE_SLOT_COUNT; i++) {
    const String p = slot_path(i);
    if (LittleFS.exists(p.c_str()) && LittleFS.remove(p.c_str())) removed++;
  }
  sNextSlot = 0;
  sSlotSeq  = 1;
  Serial.printf("[Storage] cleared %u/%u slot files\n",
                removed, (unsigned)STORAGE_SLOT_COUNT);
}

bool storage_load_latest(SlotHeader& outHeader) {
  bool found = false;
  uint32_t bestSeq = 0;
  SlotHeader candidate;

  for (uint8_t i = 0; i < STORAGE_SLOT_COUNT; i++) {
    if (!read_and_validate_slot(i, candidate)) continue;
    if (!found || candidate.slot_n > bestSeq) {
      outHeader = candidate;
      bestSeq   = candidate.slot_n;
      found     = true;
    }
  }
  return found;
}

uint8_t storage_valid_slot_count() {
  uint8_t count = 0;
  SlotHeader hdr;
  for (uint8_t i = 0; i < STORAGE_SLOT_COUNT; i++) {
    if (read_and_validate_slot(i, hdr)) count++;
  }
  return count;
}

// -- Self-test ----------------------------------------------------------------
// Writes 3 known-valid slots, corrupts the newest one's trailer, calls
// load_latest, asserts the result is slot #2 not slot #3.
//
// Intentionally destructive: clobbers existing slot files. Don't run
// during a real recording.
void storage_self_test() {
  Serial.println("[Storage self-test] start (this clobbers existing slots)");

  // Reset ring state.
  sNextSlot = 0;
  sSlotSeq  = 100;        // start high so we can tell test slots apart from real ones

  // Write 3 valid slots with monotonically increasing timestamps.
  bool ok = true;
  for (int i = 0; i < 3; i++) {
    uint32_t ts = 1000UL * (i + 1);
    if (!write_slot_at(i, ts, ts / 20, /*run_done=*/0)) ok = false;
    sSlotSeq++;
  }
  if (!ok) {
    Serial.println("[Storage self-test] FAIL: write_slot_at returned false");
    return;
  }
  Serial.printf("[Storage self-test] wrote 3 slots (seq=100..102)\n");

  // Confirm load_latest returns slot 2 (the newest, seq=102).
  SlotHeader hdr;
  if (!storage_load_latest(hdr)) {
    Serial.println("[Storage self-test] FAIL: load_latest returned false after clean writes");
    return;
  }
  if (hdr.slot_n != 102) {
    Serial.printf("[Storage self-test] FAIL: expected slot_n=102, got %lu\n",
                  (unsigned long)hdr.slot_n);
    return;
  }
  Serial.println("[Storage self-test] step 1 PASS -- load_latest returns slot 2");

  // Now corrupt slot 2's trailer to simulate a power-cut mid-write.
  String path = slot_path(2);
  File f = LittleFS.open(path.c_str(), "r+");
  if (!f) {
    Serial.println("[Storage self-test] FAIL: could not open slot 2 for corruption");
    return;
  }
  // Stomp the last 8 bytes (the trailer).
  f.seek(SS_TOTAL_BYTES - sizeof(SlotTrailer));
  uint8_t junk[sizeof(SlotTrailer)] = {0xDE, 0xAD, 0xBE, 0xEF, 0xDE, 0xAD, 0xBE, 0xEF};
  f.write(junk, sizeof(junk));
  f.close();
  Serial.println("[Storage self-test] corrupted slot 2 trailer");

  // load_latest should now return slot 1 (seq=101) since slot 2 is invalid.
  if (!storage_load_latest(hdr)) {
    Serial.println("[Storage self-test] FAIL: load_latest returned false after corruption");
    return;
  }
  if (hdr.slot_n != 101) {
    Serial.printf("[Storage self-test] FAIL: expected fallback slot_n=101, got %lu\n",
                  (unsigned long)hdr.slot_n);
    return;
  }
  Serial.println("[Storage self-test] step 2 PASS -- corrupted slot rejected, fell back to slot 1");

  // Cleanup: blow away the test slots so a real run starts clean.
  for (uint8_t i = 0; i < STORAGE_SLOT_COUNT; i++) {
    LittleFS.remove(slot_path(i).c_str());
  }
  sNextSlot = 0;
  sSlotSeq  = 1;
  Serial.println("[Storage self-test] cleanup done. PASS");
}
