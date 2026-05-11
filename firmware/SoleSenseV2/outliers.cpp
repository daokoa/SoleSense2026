// =============================================================================
// SoleSense v0.2 -- outliers.cpp
// Fixed-capacity min-heap on |sigma|. Standard textbook impl.
// =============================================================================

#include "outliers.h"

#include <math.h>

static Outlier sBuf[OUTLIER_CAPACITY];
static uint8_t sCount = 0;

static inline float key_of(const Outlier& o) {
  return o.sigma;
}

static void sift_up(uint8_t i) {
  while (i > 0) {
    uint8_t parent = (i - 1) / 2;
    if (key_of(sBuf[i]) < key_of(sBuf[parent])) {
      Outlier tmp = sBuf[i]; sBuf[i] = sBuf[parent]; sBuf[parent] = tmp;
      i = parent;
    } else break;
  }
}

static void sift_down(uint8_t i) {
  while (true) {
    uint8_t l = 2*i + 1, r = 2*i + 2, smallest = i;
    if (l < sCount && key_of(sBuf[l]) < key_of(sBuf[smallest])) smallest = l;
    if (r < sCount && key_of(sBuf[r]) < key_of(sBuf[smallest])) smallest = r;
    if (smallest == i) break;
    Outlier tmp = sBuf[i]; sBuf[i] = sBuf[smallest]; sBuf[smallest] = tmp;
    i = smallest;
  }
}

void outliers_reset() {
  sCount = 0;
}

bool outliers_offer(uint32_t ts_ms,
                    uint8_t  channel,
                    float    sample,
                    float    running_mean,
                    float    running_stddev) {
  // Guard against startup transients before stats stabilise.
  if (running_stddev < 1e-6f) return false;

  float delta = sample - running_mean;
  float sigma = fabsf(delta) / running_stddev;
  if (sigma < OUTLIER_SIGMA_THRESH) return false;

  Outlier o = { ts_ms, channel, sample, delta, sigma };

  if (sCount < OUTLIER_CAPACITY) {
    sBuf[sCount++] = o;
    sift_up(sCount - 1);
    return true;
  }

  // Full: only accept if this beats the smallest.
  if (sigma <= key_of(sBuf[0])) return false;
  sBuf[0] = o;
  sift_down(0);
  return true;
}

uint8_t outliers_count() {
  return sCount;
}

const Outlier& outliers_at(uint8_t i) {
  return sBuf[i];
}
