// =============================================================================
// SoleSense v0.2 — fft.h
// Goertzel-based incremental FFT with windowed reset.
//
// Strategy: for each (channel, bin) we run the Goertzel two-tap recurrence
// over a fixed-length window of samples. When the window completes, we read
// out the bin's complex DFT value, convert to magnitude, store it in
// sMag[channel][bin], and reset the recurrence state. The most recent
// magnitude is what fft_get_magnitude() returns.
//
// Window length is FFT_WIN_SIZE samples (~2 s at 500 Hz). Spectrum updates
// every FFT_WIN_SIZE samples per channel. Tune later (overlap, hop size).
// =============================================================================

#pragma once

#include <Arduino.h>
#include "config.h"

// Window length in samples. Must be ≥ ~Fs/lowest_bin to resolve the lowest
// frequency in the bin table. At Fs=500 Hz with 0.5 Hz lowest bin, need
// ≥ 1000; 1024 gives a 2.05 s window with ~0.49 Hz bin width — sufficient to
// distinguish cadences (1.5–3 Hz) cleanly. RAM cost per channel is independent
// of WIN_SIZE (Goertzel only stores 2 floats per bin), so window length is
// purely a frequency-resolution-vs-latency knob.
constexpr uint16_t FFT_WIN_SIZE = 1024;

// Frequencies (Hz) tracked per channel. Same set used for every channel for
// simplicity — tune per-channel later if needed.
extern const float FFT_BIN_FREQS_HZ[FFT_BINS_PER_CHAN];

// One-time setup: precompute Goertzel coefficients from FFT_BIN_FREQS_HZ +
// SAMPLE_RATE_HZ. Call from setup().
void fft_init();

// Reset all bin states + finalized magnitudes (call on /api/start).
void fft_reset();

// Push one sample into one channel's bins.
//   channel:      0..N_FSR-1 = FSR; N_FSR..N_FSR+5 = IMU axes
//   sample:       raw sample value
//   running_mean: current channel mean (subtracted before processing for DC
//                 removal — Welford-style running mean is fine)
void fft_process_sample(uint8_t channel, float sample, float running_mean);

// Magnitude of one bin (latest finalized window). Returns 0 if no window has
// completed yet for that channel. Units: same as the input signal's amplitude.
float fft_get_magnitude(uint8_t channel, uint8_t bin);

// Convenience: dominant (peak) bin for a channel — useful for cadence.
//   Returns the bin index; outMagnitude (optional) gets the peak value.
uint8_t fft_peak_bin(uint8_t channel, float* outMagnitude = nullptr);

// Number of completed windows for a channel (0 means we don't have a
// magnitude reading yet for it).
uint16_t fft_window_count(uint8_t channel);

// Optional self-test: feeds a known synthetic 2.0 Hz sine wave at amplitude
// 100 into channel 0 for one full window's worth of samples, then prints the
// resulting magnitude per bin to Serial. The 2.0 Hz bin should dominate at
// magnitude ~100, the rest near 0. Compile with FFT_SELFTEST=1 to enable.
//
// Call after Serial.begin() during setup() to run automatically; or invoke
// from a debug HTTP endpoint manually.
void fft_self_test();
