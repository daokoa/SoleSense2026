// =============================================================================
// SoleSense v0.2 — fft.cpp
// Goertzel incremental FFT.
//
// **STATUS: TASK 4 STUB.** Bin state is allocated and reset; the per-sample
// recurrence is implemented but completely untested. Magnitude calculation
// is the standard formula. Treat the numbers it returns as garbage until
// somebody validates against a known synthetic signal.
// =============================================================================

#include "fft.h"

#include <math.h>

// Default bin frequencies, suitable for 50 Hz sampling: very-low (posture),
// stride fundamental + harmonics (1–4 Hz), and impact-transient bins (5–15 Hz).
const float FFT_BIN_FREQS_HZ[FFT_BINS_PER_CHAN] = {
  0.5f,
  1.00f, 1.25f, 1.50f, 1.75f, 2.00f, 2.25f, 2.50f, 2.75f,
  3.00f, 3.50f, 4.00f,
  5.00f, 7.50f, 10.0f, 12.5f, 15.0f,
  20.0f
};

// Per-bin Goertzel coefficient = 2*cos(2π*f/Fs). Precomputed in fft_init().
static float sCoef[FFT_BINS_PER_CHAN];

// Per (channel, bin) recurrence state. Two scalars, s_{n-1} and s_{n-2}.
static float sPrev1[N_CHANNELS_TOTAL][FFT_BINS_PER_CHAN];
static float sPrev2[N_CHANNELS_TOTAL][FFT_BINS_PER_CHAN];

// Number of samples processed since last reset (used for amplitude scaling).
static uint32_t sSamplesSinceReset = 0;

void fft_init() {
  for (uint8_t b = 0; b < FFT_BINS_PER_CHAN; b++) {
    float f = FFT_BIN_FREQS_HZ[b];
    sCoef[b] = 2.0f * cosf(2.0f * (float)M_PI * f / (float)SAMPLE_RATE_HZ);
  }
  fft_reset();
}

void fft_reset() {
  for (uint8_t c = 0; c < N_CHANNELS_TOTAL; c++) {
    for (uint8_t b = 0; b < FFT_BINS_PER_CHAN; b++) {
      sPrev1[c][b] = 0.0f;
      sPrev2[c][b] = 0.0f;
    }
  }
  sSamplesSinceReset = 0;
}

void fft_process_sample(uint8_t channel, float sample) {
  if (channel >= N_CHANNELS_TOTAL) return;

  // TODO: validate this recurrence against a known signal. The standard
  //       Goertzel update is:
  //         s_n = sample + coef * s_{n-1} - s_{n-2}
  //       executed once per bin. After N samples the magnitude is read out
  //       via the formula in fft_get_magnitude().
  for (uint8_t b = 0; b < FFT_BINS_PER_CHAN; b++) {
    float s_new = sample + sCoef[b] * sPrev1[channel][b] - sPrev2[channel][b];
    sPrev2[channel][b] = sPrev1[channel][b];
    sPrev1[channel][b] = s_new;
  }
  sSamplesSinceReset++;
}

float fft_get_magnitude(uint8_t channel, uint8_t bin) {
  if (channel >= N_CHANNELS_TOTAL || bin >= FFT_BINS_PER_CHAN) return 0.0f;
  // TODO: this is the textbook magnitude formula; verify the scaling once
  //       a reference signal is fed through.
  float p1 = sPrev1[channel][bin];
  float p2 = sPrev2[channel][bin];
  float mag2 = p1*p1 + p2*p2 - sCoef[bin]*p1*p2;
  if (mag2 < 0) mag2 = 0;
  return sqrtf(mag2) / (float)(sSamplesSinceReset > 0 ? sSamplesSinceReset : 1);
}

uint8_t fft_peak_bin(uint8_t channel, float* outMagnitude) {
  uint8_t best = 0;
  float bestMag = 0.0f;
  for (uint8_t b = 0; b < FFT_BINS_PER_CHAN; b++) {
    float m = fft_get_magnitude(channel, b);
    if (m > bestMag) { bestMag = m; best = b; }
  }
  if (outMagnitude) *outMagnitude = bestMag;
  return best;
}
