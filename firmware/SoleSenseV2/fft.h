// =============================================================================
// SoleSense v0.2 — fft.h
// Goertzel-based incremental FFT. For each of N_CHANNELS_TOTAL channels we
// track FFT_BINS_PER_CHAN frequency bins.
//
// **STATUS: PARTIALLY STUBBED.** fft_init() and fft_reset() are real;
// fft_process_sample() and fft_get_magnitude() are placeholders that compile
// but return zeros. See docs/superpowers/plans/2026-05-06-v0.2-firmware.md
// Task 4 for the implementation guide.
// =============================================================================

#pragma once

#include <Arduino.h>
#include "config.h"

// Frequencies (Hz) tracked per channel. Same set used for every channel for
// simplicity — tune per-channel later if a channel benefits from different bins.
extern const float FFT_BIN_FREQS_HZ[FFT_BINS_PER_CHAN];

// One-time setup (precompute Goertzel coefficients from FFT_BIN_FREQS_HZ +
// SAMPLE_RATE_HZ). Call from setup().
void fft_init();

// Reset all bin states (call on /api/start).
void fft_reset();

// Push one sample into one channel's bins.
//   channel: 0..N_FSR-1 = FSR; N_FSR..N_FSR+5 = IMU axes (ax, ay, az, gx, gy, gz)
void fft_process_sample(uint8_t channel, float sample);

// Magnitude of one bin in one channel. Computed on demand from current state.
float fft_get_magnitude(uint8_t channel, uint8_t bin);

// Convenience: dominant (peak) bin for a channel — useful for cadence.
//   Returns the bin index; pass nullptr for outMagnitude if you don't need it.
uint8_t fft_peak_bin(uint8_t channel, float* outMagnitude = nullptr);
