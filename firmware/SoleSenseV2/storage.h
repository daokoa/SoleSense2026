// =============================================================================
// SoleSense v0.2 -- storage.h
// Multi-slot ring buffer in LittleFS. Each slot holds a snapshot of run state
// (FFT bin states, outlier buffer, run metadata). On boot or reconnect we
// scan all slots and pick the one with the highest valid timestamp.
//
// Full snapshot serialization: FFT magnitudes + Goertzel filter state +
// outlier buffer + Welford stats + run aggregates, with header + trailer
// magic + CRC32 for atomic-tear and bit-rot detection.
// =============================================================================

#pragma once

#include <Arduino.h>
#include "config.h"

struct __attribute__((packed)) SlotHeader {
  uint32_t magic;           // STORAGE_MAGIC_HEADER
  uint32_t slot_n;          // 0..STORAGE_SLOT_COUNT-1, monotonic increasing
  uint32_t timestamp_ms;    // run-elapsed time at this snapshot
  uint32_t sample_count;    // total samples processed
  uint32_t run_done;        // 1 if /api/stop was hit before this snapshot
  uint32_t body_bytes;      // bytes in the body (for forward compat)
};

struct __attribute__((packed)) SlotTrailer {
  uint32_t crc32;           // CRC32 over header + body
  uint32_t magic;           // STORAGE_MAGIC_TRAILER (proves write completed)
};

// Scalar run state needed to rebuild /api/run-report from a slot (the FFT
// magnitudes are stored separately in the slot body). Stored where the old
// 32-byte "reserved" area used to be. Versioned for forward compat.
constexpr uint32_t RUNMETA_VERSION = 1;
struct __attribute__((packed)) RunMeta {
  uint32_t version;            // RUNMETA_VERSION
  uint32_t stepCount;          // gStepCount (single-foot strikes)
  uint32_t contactSumMs;       // gContactSumMs
  uint32_t contactCount;       // gContactCount
  uint32_t imuImpactCount;     // gImuImpactCount
  uint32_t imuConnected;       // gImuConnected (bool)
  float    maxHeelJerk;        // gMaxHeelJerk
  float    maxJerkZ;           // gMaxJerkZ
  float    maxTotalPressure;   // gMaxTotalPressure
  float    effectiveSampleRate;// gEffectiveSampleRate
  float    bodyKg;             // session body weight snapshot
  float    heightCm;           // session height snapshot
  float    statMean  [N_CHANNELS_TOTAL];   // Welford mean per channel
  float    statStddev[N_CHANNELS_TOTAL];   // Welford stddev per channel
  uint32_t statN     [N_CHANNELS_TOTAL];   // sample count per channel
};

// One-time setup. Opens or creates /run.bin and reserves
// STORAGE_SLOT_COUNT * STORAGE_SLOT_SIZE_BYTES of LittleFS space.
void storage_init();

// Called on /api/start. Resets the in-RAM ring index; doesn't touch flash.
void storage_begin_run();

// Called on /api/stop. Writes a final snapshot with run_done=1.
void storage_end_run(uint32_t elapsed_ms, uint32_t sample_count);

// Periodic snapshot (driven from state.cpp every STORAGE_FLUSH_MS).
// Serialises FFT state + outlier buffer + the supplied metadata to the next
// slot in the ring; advances the ring index.
void storage_save_snapshot(uint32_t elapsed_ms, uint32_t sample_count);

// Delete every /run_slot_N.bin file on LittleFS and reset the in-RAM ring
// pointer. Called from /api/data/clear; next storage_begin_run() reinitialises.
// In-memory run aggregates (gMaxTotalPressure, etc.) are reset by the state
// machine on the next Start Run, not by this call.
void storage_clear();

// Scan all slots, validate trailers, return the header of the newest valid
// snapshot (the one with the highest timestamp_ms). Returns true if any slot
// was valid; false if /run.bin is empty/corrupt.
//
// Used on boot and when the phone reconnects to display the run-in-progress.
bool storage_load_latest(SlotHeader& outHeader);

// Restore the newest valid slot's run state into RAM: the scalar aggregates
// (step count, contact, jerk, max pressure, profile), the per-channel Welford
// stats, and the FFT magnitudes. After this, /api/run-report reconstructs the
// last run even though RAM was cleared by a reboot/power-cut. Returns true if
// a slot was restored. Call once on boot, after storage_init(). Does NOT set
// gState to recording -- it only repopulates the report inputs.
bool storage_restore_latest();

// Diagnostic: how many slots currently contain valid data.
uint8_t storage_valid_slot_count();

// Destructive self-test: writes 3 known slots, corrupts the newest, asserts
// that load_latest() returns the second-newest. Cleans up after itself.
// Output goes to Serial Monitor. Don't run during a real recording.
void storage_self_test();
