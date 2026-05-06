// =============================================================================
// SoleSense v0.2 — state.cpp
// =============================================================================

#include "state.h"
#include "config.h"
#include "sensors.h"
#include "fft.h"
#include "outliers.h"
#include "storage.h"

#include <WiFi.h>

volatile RunState gState        = RS_IDLE;
volatile bool gRunActive        = false;
volatile uint32_t gRunElapsedMs = 0;
volatile uint32_t gLastActiveMs = 0;
volatile uint32_t gSampleCount  = 0;
volatile bool gNewSample        = false;
volatile bool gStartRequested   = false;
volatile bool gStopRequested    = false;
volatile bool gSleepRequested   = false;
volatile uint32_t gPauseStartMs = 0;

static uint32_t sLastFlushMs = 0;

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
  gLastActiveMs = millis();
  gPauseStartMs = 0;
  fft_reset();
  outliers_reset();
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
