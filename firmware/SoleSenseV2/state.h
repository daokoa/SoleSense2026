// =============================================================================
// SoleSense v0.2 — state.h
// Run state machine + request flags. The run is a simple state machine with
// a "paused" sub-state derived from AP client count.
// =============================================================================

#pragma once

#include <Arduino.h>

enum RunState : uint8_t { RS_IDLE = 0, RS_RECORDING = 1 };

// Top-level state. Set/cleared by the start/stop request handlers via the
// loop()-driven state machine in state.cpp.
extern volatile RunState gState;

// True only when gState==RS_RECORDING AND at least one client is on the AP.
// Sampling, FFT updates, and slot flushes only happen when this is true.
extern volatile bool gRunActive;

// Elapsed run time in ms, accumulated from intervals where gRunActive was true.
// THIS IS THE AUTHORITATIVE TIMER. Read by /api/run-state. Persisted to flash
// via storage.cpp.
extern volatile uint32_t gRunElapsedMs;

// Last millis() value when sampling was active. Used by state.cpp to advance
// gRunElapsedMs by the active delta on each loop tick.
extern volatile uint32_t gLastActiveMs;

// Total sample count for the current run (rolls back to 0 on /api/start).
extern volatile uint32_t gSampleCount;

// Peak vertical jerk (|d(accel_z)/dt|) seen during the current run, in m/s³.
// FSR 402 saturates at ~10 kg so it can't measure peak running force directly
// (running impact is 100–200 kg of ground reaction). Vertical jerk from the
// IMU does measure impact rate cleanly — dividing by g (9.81 m/s²) gives the
// loading rate in BW/s, the standard biomechanics unit. Reset to 0 on
// /api/start. Updated per sample by process_sample() in SoleSenseV2.ino.
extern volatile float gMaxJerkZ;

// Set true by the 50Hz hardware-timer ISR. loop() drains this flag.
extern volatile bool gNewSample;

// HTTP-task → loop() request flags. Handlers set these and return immediately;
// loop() observes them and performs the state transition.
extern volatile bool gStartRequested;
extern volatile bool gStopRequested;
extern volatile bool gSleepRequested;

// Pause-on-disconnect bookkeeping (state.cpp).
extern volatile uint32_t gPauseStartMs;     // millis() when last paused, 0 if not paused

// Initialise state globals to known values (call from setup()).
void state_init();

// Drive the state machine: process request flags, observe AP station count,
// pause/resume gRunActive accordingly, advance gRunElapsedMs.
// Call once per loop() iteration before takeSample().
void state_tick();

// Convenience: human-readable label for the current state.
const char* state_name();
