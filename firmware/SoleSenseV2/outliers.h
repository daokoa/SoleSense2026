// =============================================================================
// SoleSense v0.2 — outliers.h
// Top-N min-heap of outlier samples, ordered by absolute magnitude. New
// outliers above OUTLIER_SIGMA_THRESH push out the smallest tracked outlier
// once the buffer is full.
// =============================================================================

#pragma once

#include <Arduino.h>
#include "config.h"

struct Outlier {
  uint32_t timestamp_ms;   // run-elapsed time when this happened
  uint8_t  channel;        // 0..N_FSR-1 = FSR; N_FSR..N_FSR+5 = IMU axes
  float    value;          // raw value (post-offset)
  float    delta;          // sample - running_mean (signed)
  float    sigma;          // |delta| / running_stddev
};

// One-time init / clear (call on /api/start).
void outliers_reset();

// Test a sample against current statistics; if it's an outlier, push it onto
// the heap (evicting the smallest by |sigma| if at capacity). Caller passes
// the running mean and stddev for that channel (maintained elsewhere).
//
// Returns true if the sample was an outlier (was pushed onto the heap).
bool outliers_offer(uint32_t ts_ms,
                    uint8_t  channel,
                    float    sample,
                    float    running_mean,
                    float    running_stddev);

// Number of outliers currently held (≤ OUTLIER_CAPACITY).
uint8_t outliers_count();

// Read access (used by /api/run-outliers and storage.cpp).
const Outlier& outliers_at(uint8_t i);
