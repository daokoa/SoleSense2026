// =============================================================================
// SoleSense v0.2 -- state.cpp
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
volatile bool gResetSampleTracking = false;

volatile uint32_t gStepCount       = 0;
volatile uint32_t gContactSumMs    = 0;
volatile uint32_t gContactCount    = 0;
volatile float    gMaxHeelJerk     = 0.0f;
volatile uint32_t gLastImuImpactMs = 0;
volatile bool     gImuConnected    = false;
volatile uint32_t gImuImpactCount  = 0;
volatile float    gMaxTotalPressure = 0.0f;

static uint32_t sLastFlushMs = 0;

// Step-detector state (private to this translation unit; reset at /api/start).
static bool     sHeelInContact          = false;
static uint32_t sStepContactStartMs     = 0;
static uint32_t sStepLastImpactMs       = 0;
static float    sStepPeak               = 0.0f;   // peak heel ADC seen during current contact

// 2-state constant-velocity Kalman filter on the heel composite.
// x = [pressure, velocity]^T, F = [[1, dt],[0, 1]], H = [1, 0].
// Q derived from a white-noise-acceleration model (sigma_a ~ 1e5 ADC/s^2);
// R = 25 approximates 5 LSB ADC noise variance.
struct KalmanCV {
  float x0, x1;                 // state: pressure, velocity (ADC counts, counts/s)
  float P00, P01, P10, P11;     // 2x2 covariance
  static constexpr float DT  = 1.0f / (float)SAMPLE_RATE_HZ;
  static constexpr float Q00 = 4.0e-2f;
  static constexpr float Q01 = 4.0e1f;
  static constexpr float Q11 = 4.0e4f;
  static constexpr float R   = 25.0f;

  void reset() {
    x0 = x1 = 0.0f;
    P00 = 1000.0f; P01 = 0.0f; P10 = 0.0f; P11 = 1000.0f;
  }

  void update(float z) {
    // Predict
    x0 = x0 + DT * x1;
    float pp00 = P00 + DT * (P10 + P01) + DT * DT * P11 + Q00;
    float pp01 = P01 + DT * P11 + Q01;
    float pp10 = P10 + DT * P11 + Q01;
    float pp11 = P11 + Q11;
    // Update (innovation y = z - H x, S = H P H^T + R = pp00 + R)
    float S  = pp00 + R;
    float K0 = pp00 / S;
    float K1 = pp10 / S;
    float y  = z - x0;
    x0 += K0 * y;
    x1 += K1 * y;
    // Posterior covariance: (I - K H) P
    P00 = (1.0f - K0) * pp00;
    P01 = (1.0f - K0) * pp01;
    P10 = -K1 * pp00 + pp10;
    P11 = -K1 * pp01 + pp11;
  }
};
static KalmanCV sHeelKalman;

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
  gStepCount        = 0;
  gContactSumMs     = 0;
  gContactCount     = 0;
  gMaxHeelJerk      = 0.0f;
  gLastImuImpactMs  = 0;
  gImuConnected     = false;
  gImuImpactCount   = 0;
  gMaxTotalPressure = 0.0f;
  sHeelInContact      = false;
  sStepContactStartMs = 0;
  sStepLastImpactMs   = 0;
  sStepPeak           = 0.0f;
  sHeelKalman.reset();
  fft_reset();
  outliers_reset();
  stats_reset();
  storage_begin_run();
  gState     = RS_RECORDING;
  gRunActive = WiFi.softAPgetStationNum() > 0;
  gResetSampleTracking = true;   // first sample after start gets a clean derivative-history
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
    // Just lost all clients -- pause.
    gRunElapsedMs += (now - gLastActiveMs);   // commit the in-progress active interval
    gRunActive    = false;
    gPauseStartMs = now;
    Serial.println("[Run] paused (no clients)");
  } else if (!gRunActive && clients > 0) {
    // Just regained a client -- resume. Reset the jerk-prev tracking so
    // the next sample doesn't compute a giant spurious jerk over the
    // pause-duration gap and falsely fire the high_loading flag.
    gLastActiveMs = now;
    gRunActive    = true;
    gResetSampleTracking = true;
    Serial.printf("[Run] resumed after %lu ms paused\n",
                  (unsigned long)(now - gPauseStartMs));
    gPauseStartMs = 0;
  } else if (gRunActive) {
    // Still active -- advance the running-elapsed cheaply (small per-tick adds keep
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

// Time-domain step detector.
//   STRIKE  = filtered pressure > RISE_THRESHOLD and velocity > RISE_VEL,
//             plus refractory + IMU validation
//   TOE_OFF = filtered velocity < FALL_VEL or contact > MAX_CONTACT_MS
//   Step counted only if contact duration falls in [MIN, MAX].
void step_detector_update(float heelValue, float heelMean, float heelStddev,
                          uint32_t nowMs) {
  (void)heelMean;
  (void)heelStddev;
  if (gState != RS_RECORDING) return;

  sHeelKalman.update(heelValue);
  const float p = sHeelKalman.x0;   // filtered pressure (ADC counts)
  const float v = sHeelKalman.x1;   // filtered velocity (ADC counts / sec)

  constexpr float    STEP_RISE_THRESHOLD =  400.0f;   // filtered pressure floor
  constexpr float    STEP_RISE_VEL       =  8000.0f;  // rising at >= this rate
  constexpr float    STEP_FALL_VEL       = -5000.0f;  // falling at <= this rate
  constexpr uint32_t STEP_REFRACTORY_MS  = 250;       // covers a stride contact
  constexpr uint32_t MIN_CONTACT_MS      = 50;        // shorter = bounce, drop
  constexpr uint32_t MAX_CONTACT_MS      = 800;       // longer  = lean, drop

  // IMU sensor-fusion gate (only when IMU is producing real samples).
  constexpr uint32_t IMU_IMPACT_WINDOW_MS = 100;
  const bool imuValidated = !gImuConnected ||
                            (nowMs - gLastImuImpactMs) <= IMU_IMPACT_WINDOW_MS;

  // -- STRIKE ----------------------------------------------------------------
  // Pressure must clear an absolute floor AND velocity must be clearly
  // positive -- that's what distinguishes the real leading edge of a press
  // from the secondary peaks during ringing (where v oscillates near 0).
  if (!sHeelInContact
      && p > STEP_RISE_THRESHOLD
      && v > STEP_RISE_VEL
      && (nowMs - sStepLastImpactMs) > STEP_REFRACTORY_MS
      && imuValidated) {
    sHeelInContact      = true;
    sStepContactStartMs = nowMs;
    sStepLastImpactMs   = nowMs;
    sStepPeak           = p;
    Serial.printf("[Step] tentative   p=%.0f v=%.0f @ %lu ms %s\n",
                  p, v, (unsigned long)nowMs,
                  gImuConnected ? "[IMU-validated]" : "[FSR-only]");
    return;
  }

  if (!sHeelInContact) return;

  // -- IN-CONTACT -----------------------------------------------------------
  if (p > sStepPeak) sStepPeak = p;

  const uint32_t contactSoFar = nowMs - sStepContactStartMs;
  const bool velocityRelease  = v < STEP_FALL_VEL;
  const bool timeoutRelease   = contactSoFar > MAX_CONTACT_MS;

  if (velocityRelease || timeoutRelease) {
    sHeelInContact = false;
    const bool valid = (contactSoFar >= MIN_CONTACT_MS &&
                        contactSoFar <= MAX_CONTACT_MS);
    if (valid) {
      gStepCount++;
      gContactSumMs += contactSoFar;
      gContactCount++;
    }
    Serial.printf("[Step] %s p=%.0f v=%.0f peak=%.0f contact=%lu ms #%lu%s\n",
                  valid ? "STEP    " : "discard ",
                  p, v, sStepPeak, (unsigned long)contactSoFar,
                  (unsigned long)gStepCount,
                  timeoutRelease ? " TIMEOUT" : "");
    if (timeoutRelease) {
      // Don't immediately re-strike on a still-high signal -- bump the
      // refractory window so the user must release-and-press again.
      sStepLastImpactMs = nowMs;
    }
  }
}
