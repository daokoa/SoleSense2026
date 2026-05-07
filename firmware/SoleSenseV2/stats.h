// =============================================================================
// SoleSense v0.2 — stats.h
// Per-channel Welford online mean + variance. Used by:
//   - outliers.cpp (sigma-threshold detection)
//   - fft.cpp (DC removal)
//   - http_routes.cpp run-report (zone distribution, balance, pronation)
// =============================================================================

#pragma once

#include <Arduino.h>
#include "config.h"

// Reset all per-channel stats (call on /api/start).
void stats_reset();

// Update one channel's running mean and variance with one new sample.
// Welford's online algorithm — numerically stable, O(1) per sample.
void stats_update(uint8_t channel, float sample);

// Read the running mean of one channel.
float stats_get_mean(uint8_t channel);

// Read the running standard deviation of one channel (returns 0 until n ≥ 2).
float stats_get_stddev(uint8_t channel);

// Number of samples seen by this channel since last reset.
uint32_t stats_get_count(uint8_t channel);
