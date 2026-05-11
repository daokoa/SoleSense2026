// =============================================================================
// SoleSense v0.2 -- stats.cpp
// =============================================================================

#include "stats.h"

#include <math.h>

static float    sMean[N_CHANNELS_TOTAL];
static float    sM2  [N_CHANNELS_TOTAL];
static uint32_t sN   [N_CHANNELS_TOTAL];

void stats_reset() {
  for (uint8_t c = 0; c < N_CHANNELS_TOTAL; c++) {
    sMean[c] = 0.0f;
    sM2  [c] = 0.0f;
    sN   [c] = 0;
  }
}

void stats_update(uint8_t c, float x) {
  if (c >= N_CHANNELS_TOTAL) return;
  sN[c]++;
  float delta1 = x - sMean[c];
  sMean[c] += delta1 / (float)sN[c];
  float delta2 = x - sMean[c];
  sM2[c] += delta1 * delta2;
}

float stats_get_mean(uint8_t c) {
  if (c >= N_CHANNELS_TOTAL) return 0.0f;
  return sMean[c];
}

float stats_get_stddev(uint8_t c) {
  if (c >= N_CHANNELS_TOTAL || sN[c] <= 1) return 0.0f;
  return sqrtf(sM2[c] / (float)(sN[c] - 1));
}

uint32_t stats_get_count(uint8_t c) {
  if (c >= N_CHANNELS_TOTAL) return 0;
  return sN[c];
}
