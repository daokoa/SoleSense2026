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

static uint32_t sLastFlushMs = 0;

// Step-detector state (private to this translation unit; reset at /api/start).
static bool     sHeelInContact          = false;
static uint32_t sStepContactStartMs     = 0;
static uint32_t sStepLastImpactMs       = 0;

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
  sHeelInContact      = false;
  sStepContactStartMs = 0;
  sStepLastImpactMs   = 0;
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
// Schmitt-trigger on the heel channel. Rising edge above ABSOLUTE FSR floor is
// a heel-strike; falling edge below the release floor is a toe-off. The
// refractory period (150 ms ≈ 6.7 Hz cap) suppresses double-counting on FSR
// bounce or fingertip lift-tap. Contact intervals outside [50, 800] ms are
// dropped from the GCT average — anything shorter is debounce noise, anything
// longer is leaning rather than a step.
//
// We use absolute ADC values (not mean-relative) on purpose: the Welford
// running mean would drift upward with every press and the running σ would
// be poisoned by the press samples themselves, killing detection of the
// second-and-later presses. The FSR rests around 0–150 ADC and a real press
// reaches 500–3000+, so a fixed 400-ADC cutoff cleanly separates them
// without any mean/σ tracking. heelMean / heelStddev are still passed in for
// future EMA-baseline work but currently unused.
void step_detector_update(float heelValue, float heelMean, float heelStddev,
                          uint32_t nowMs) {
  (void)heelMean;
  (void)heelStddev;
  if (gState != RS_RECORDING) return;

  constexpr float    STEP_RISE_THRESHOLD = 400.0f;   // raw ADC: clearly pressed
  constexpr float    STEP_FALL_THRESHOLD = 200.0f;   // raw ADC: clearly released
  constexpr uint32_t STEP_REFRACTORY_MS  = 150;      // min interval between strikes
  constexpr uint32_t MIN_CONTACT_MS      = 50;       // shorter = bounce, drop
  constexpr uint32_t MAX_CONTACT_MS      = 800;      // longer  = lean, drop

  if (!sHeelInContact
      && heelValue > STEP_RISE_THRESHOLD
      && (nowMs - sStepLastImpactMs) > STEP_REFRACTORY_MS) {
    // Heel strike.
    sHeelInContact      = true;
    sStepContactStartMs = nowMs;
    sStepLastImpactMs   = nowMs;
    gStepCount++;
  } else if (sHeelInContact && heelValue < STEP_FALL_THRESHOLD) {
    // Toe-off.
    sHeelInContact = false;
    uint32_t contactMs = nowMs - sStepContactStartMs;
    if (contactMs >= MIN_CONTACT_MS && contactMs <= MAX_CONTACT_MS) {
      gContactSumMs += contactMs;
      gContactCount++;
    }
  }
}
