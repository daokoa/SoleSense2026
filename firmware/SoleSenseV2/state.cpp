// =============================================================================
// SoleSense v0.2 — state.cpp
// =============================================================================

#include "state.h"
#include "config.h"
#include "sensors.h"
#include "fft.h"
#include "outliers.h"
#include "stats.h"
#include "storage.h"

#include <WiFi.h>

volatile RunState gState        = RS_IDLE;
volatile bool gRunActive        = false;
volatile uint32_t gRunElapsedMs = 0;
volatile uint32_t gLastActiveMs = 0;
volatile uint32_t gSampleCount  = 0;
volatile float    gMaxJerkZ     = 0.0f;
volatile bool gNewSample        = false;
volatile bool gStartRequested   = false;
volatile bool gStopRequested    = false;
volatile bool gSleepRequested   = false;
volatile uint32_t gPauseStartMs = 0;

volatile uint32_t gStepCount    = 0;
volatile uint32_t gContactSumMs = 0;
volatile uint32_t gContactCount = 0;
volatile float    gMaxHeelJerk  = 0.0f;

static uint32_t sLastFlushMs = 0;

// Step-detector state (private to this translation unit; reset at /api/start).
static bool     sHeelInContact          = false;
static uint32_t sStepContactStartMs     = 0;
static uint32_t sStepLastImpactMs       = 0;
static float    sStepPeak               = 0.0f;   // peak heel ADC seen during current contact

void state_init() {
  gState           = RS_IDLE;
  gRunActive       = false;
  gRunElapsedMs    = 0;
  gLastActiveMs    = 0;
  gSampleCount     = 0;
  gNewSample       = false;
  gStartRequested  = false;
  gStopRequested   = false;
  gSleepRequested  = false;
  gPauseStartMs    = 0;
  sLastFlushMs     = 0;
}

static void enter_recording() {
  // Reset all per-run state.
  gRunElapsedMs = 0;
  gSampleCount  = 0;
  gMaxJerkZ     = 0.0f;
  gLastActiveMs = millis();
  gPauseStartMs = 0;
  gStepCount    = 0;
  gContactSumMs = 0;
  gContactCount = 0;
  gMaxHeelJerk  = 0.0f;
  sHeelInContact      = false;
  sStepContactStartMs = 0;
  sStepLastImpactMs   = 0;
  sStepPeak           = 0.0f;
  fft_reset();
  outliers_reset();
  stats_reset();
  storage_begin_run();
  gState     = RS_RECORDING;
  gRunActive = WiFi.softAPgetStationNum() > 0;
  Serial.println("[Run] started");
}

static void exit_recording() {
  gRunActive = false;
  storage_end_run(gRunElapsedMs, gSampleCount);
  gState = RS_IDLE;
  Serial.printf("[Run] stopped after %lu ms / %lu samples\n",
                (unsigned long)gRunElapsedMs, (unsigned long)gSampleCount);
}

void state_tick() {
  // 1. Service request flags (set by HTTP handlers on the AsyncWebServer task).
  if (gStartRequested) {
    gStartRequested = false;
    if (gState == RS_IDLE) enter_recording();
  }
  if (gStopRequested) {
    gStopRequested = false;
    if (gState == RS_RECORDING) exit_recording();
  }
  // gSleepRequested is consumed by SoleSenseV2.ino directly (calls deep_sleep()).

  // 2. Pause-on-disconnect (only relevant while recording).
  if (gState != RS_RECORDING) return;

  uint8_t clients = WiFi.softAPgetStationNum();
  uint32_t now = millis();

  if (gRunActive && clients == 0) {
    // Just lost all clients — pause.
    gRunElapsedMs += (now - gLastActiveMs);   // commit the in-progress active interval
    gRunActive    = false;
    gPauseStartMs = now;
    Serial.println("[Run] paused (no clients)");
  } else if (!gRunActive && clients > 0) {
    // Just regained a client — resume.
    gLastActiveMs = now;
    gRunActive    = true;
    Serial.printf("[Run] resumed after %lu ms paused\n",
                  (unsigned long)(now - gPauseStartMs));
    gPauseStartMs = 0;
  } else if (gRunActive) {
    // Still active — advance the running-elapsed cheaply (small per-tick adds keep
    // /api/run-state responsive even between flushes).
    gRunElapsedMs += (now - gLastActiveMs);
    gLastActiveMs = now;
  }

  // 3. Periodic flash flush (only while active).
  if (gRunActive && (now - sLastFlushMs) >= STORAGE_FLUSH_MS) {
    sLastFlushMs = now;
    storage_save_snapshot(gRunElapsedMs, gSampleCount);
  }
}

const char* state_name() {
  return gState == RS_IDLE ? "idle" : "recording";
}

// ── Time-domain step detector ─────────────────────────────────────────────────
// Schmitt-trigger on the heel channel with a *peak-relative* fall threshold so
// it survives FSR baseline drift. The previous absolute fall (heel < 200 ADC)
// silently broke when the FSR's at-rest reading sat above 200: the detector
// fired once on the first press, then never re-armed for the entire run.
//
// Algorithm per sample, while heel composite = max(ch0, ch1):
//   - Idle, heel > RISE_THRESHOLD, refractory expired → STRIKE.
//                Track sStepPeak from the strike onward.
//   - In-contact, heel > sStepPeak → update peak.
//   - In-contact, heel < sStepPeak × FALL_FRACTION → TOE-OFF.
//   - In-contact, contactMs > MAX_CONTACT_MS → TIMEOUT release (force-clear,
//                bump refractory so we don't immediately re-strike on a
//                still-high signal).
//
// Contact intervals outside [50, 800] ms are dropped from the GCT average:
// shorter is debounce noise, longer is a lean rather than a step. The 150 ms
// refractory caps detected cadence at ≈6.7 Hz — well above any real running
// stride rate.
//
// heelMean / heelStddev are still passed in for future EMA-baseline work but
// currently unused.
void step_detector_update(float heelValue, float heelMean, float heelStddev,
                          uint32_t nowMs) {
  (void)heelMean;
  (void)heelStddev;
  if (gState != RS_RECORDING) return;

  constexpr float    STEP_RISE_THRESHOLD = 400.0f;   // raw ADC: clearly pressed
  constexpr float    STEP_FALL_FRACTION  = 0.5f;     // fall < peak × this
  constexpr uint32_t STEP_REFRACTORY_MS  = 150;      // min interval between strikes
  constexpr uint32_t MIN_CONTACT_MS      = 50;       // shorter = bounce, drop
  constexpr uint32_t MAX_CONTACT_MS      = 800;      // longer  = lean, force-release

  if (!sHeelInContact
      && heelValue > STEP_RISE_THRESHOLD
      && (nowMs - sStepLastImpactMs) > STEP_REFRACTORY_MS) {
    // Tentative strike: start tracking contact, but DON'T increment gStepCount
    // yet. We only credit a step when the contact passes the validity gate at
    // toe-off. FSR signals ring during a single physical press (rises, drops
    // below peak/2, rises again), and without this gate every ring counts as
    // a step — that's the "98 steps from 6 presses" bug.
    sHeelInContact      = true;
    sStepContactStartMs = nowMs;
    sStepLastImpactMs   = nowMs;
    sStepPeak           = heelValue;
    Serial.printf("[Step] tentative   heel=%.0f @ %lu ms\n",
                  heelValue, (unsigned long)nowMs);
  } else if (sHeelInContact) {
    // Track impact peak so the relative fall threshold scales with each strike.
    if (heelValue > sStepPeak) sStepPeak = heelValue;

    uint32_t contactSoFar  = nowMs - sStepContactStartMs;
    bool relativeRelease   = heelValue < sStepPeak * STEP_FALL_FRACTION;
    bool timeoutRelease    = contactSoFar > MAX_CONTACT_MS;

    if (relativeRelease || timeoutRelease) {
      sHeelInContact = false;
      bool valid = (contactSoFar >= MIN_CONTACT_MS && contactSoFar <= MAX_CONTACT_MS);
      if (valid) {
        gStepCount++;
        gContactSumMs += contactSoFar;
        gContactCount++;
      }
      Serial.printf("[Step] %s heel=%.0f peak=%.0f contact=%lu ms #%lu%s\n",
                    valid ? "STEP    " : "discard ",
                    heelValue, sStepPeak, (unsigned long)contactSoFar,
                    (unsigned long)gStepCount,
                    timeoutRelease ? " TIMEOUT" : "");
      if (timeoutRelease) {
        // Don't immediately re-strike on a still-high signal — bump the
        // refractory window so the user must release-and-press again.
        sStepLastImpactMs = nowMs;
      }
    }
  }
}
