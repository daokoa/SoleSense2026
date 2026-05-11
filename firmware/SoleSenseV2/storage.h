// =============================================================================
// SoleSense v0.2 -- storage.h
// Multi-slot ring buffer in LittleFS. Each slot holds a snapshot of run state
// (FFT bin states, outlier buffer, run metadata). On boot or reconnect we
// scan all slots and pick the one with the highest valid timestamp.
//
// **STATUS: PARTIALLY STUBBED.** Layout structs and the file-init function
// are real; storage_save_snapshot() and storage_load_latest() are no-ops.
// See docs/design/plans/2026-05-06-v0.2-firmware.md Task 5.
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

// Diagnostic: how many slots currently contain valid data.
uint8_t storage_valid_slot_count();

// Destructive self-test: writes 3 known slots, corrupts the newest, asserts
// that load_latest() returns the second-newest. Cleans up after itself.
// Output goes to Serial Monitor. Don't run during a real recording.
void storage_self_test();
