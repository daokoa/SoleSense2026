// =============================================================================
// SoleSense v0.2 — storage.cpp
//
// **STATUS: TASK 5 STUB.** init+begin+end logged; save/load are no-ops.
// =============================================================================

#include "storage.h"
#include "fft.h"
#include "outliers.h"

#include <LittleFS.h>

static uint8_t  sNextSlot = 0;     // index of slot to write next
static uint32_t sSlotSeq  = 0;     // monotonically increasing sequence

void storage_init() {
  if (!LittleFS.exists(STORAGE_FILENAME)) {
    File f = LittleFS.open(STORAGE_FILENAME, "w");
    if (!f) {
      Serial.println("[Storage] init FAILED — could not create file");
      return;
    }
    // Pre-allocate so the slot offsets are stable.
    static uint8_t zero[256];
    memset(zero, 0, sizeof(zero));
    uint32_t total = (uint32_t)STORAGE_SLOT_COUNT * STORAGE_SLOT_SIZE_BYTES;
    for (uint32_t w = 0; w < total; w += sizeof(zero)) {
      f.write(zero, sizeof(zero));
    }
    f.close();
    Serial.printf("[Storage] created %s (%u bytes, %u slots)\n",
                  STORAGE_FILENAME, (unsigned)total, (unsigned)STORAGE_SLOT_COUNT);
  } else {
    Serial.printf("[Storage] /run.bin exists\n");
  }
}

void storage_begin_run() {
  sNextSlot = 0;
  sSlotSeq  = 0;
  Serial.println("[Storage] run begin (ring reset)");
}

void storage_save_snapshot(uint32_t elapsed_ms, uint32_t sample_count) {
  // TODO Task 5:
  //   1. Build SlotHeader { magic, slot_n=sSlotSeq, timestamp_ms=elapsed_ms,
  //                          sample_count, run_done=0, body_bytes=... }
  //   2. Serialize FFT state arrays + outlier buffer + meta into a buffer.
  //   3. Compute CRC32 over header+body using crc32_le() from <rom/crc.h>.
  //   4. Open /run.bin, seek to (sNextSlot * STORAGE_SLOT_SIZE_BYTES),
  //      write header, body, trailer. Close.
  //   5. sNextSlot = (sNextSlot + 1) % STORAGE_SLOT_COUNT;
  //   6. sSlotSeq++.
  //
  // Keep total write under STORAGE_SLOT_SIZE_BYTES. Body for current size:
  //   FFT state: N_CHANNELS_TOTAL * FFT_BINS_PER_CHAN * 2 * sizeof(float)
  //              = 12 * 18 * 8 = 1728 bytes
  //   Outliers : OUTLIER_CAPACITY * sizeof(Outlier)
  //              = 100 * 24 = 2400 bytes
  //   Meta     : ~64 bytes
  //   Total    : ~4 KB plus header/trailer overhead. Well under 10 KB slot.
  (void)elapsed_ms; (void)sample_count;
}

void storage_end_run(uint32_t elapsed_ms, uint32_t sample_count) {
  // TODO Task 5: same as save_snapshot but with run_done=1.
  storage_save_snapshot(elapsed_ms, sample_count);
}

bool storage_load_latest(SlotHeader& outHeader) {
  // TODO Task 5:
  //   for slot 0..STORAGE_SLOT_COUNT-1:
  //     seek, read header (sizeof(SlotHeader))
  //     if header.magic != STORAGE_MAGIC_HEADER: skip
  //     seek to end-of-slot, read trailer
  //     if trailer.magic != STORAGE_MAGIC_TRAILER: skip
  //     compute CRC32 over header+body, compare to trailer.crc32; mismatch -> skip
  //     keep the header with highest timestamp_ms.
  //   return true if any kept; copy that header to outHeader.
  (void)outHeader;
  return false;
}

uint8_t storage_valid_slot_count() {
  // TODO Task 5: same scan loop as load_latest, but just count valid slots.
  return 0;
}
