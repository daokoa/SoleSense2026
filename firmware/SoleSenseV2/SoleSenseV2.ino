// SoleSense v0.2 main sketch.
// Architecture: docs/design/specs/2026-05-06-v0.2-data-architecture.md

#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <esp_sleep.h>
#include <math.h>

#include "config.h"
#include "state.h"
#include "sensors.h"
#include "fft.h"
#include "outliers.h"
#include "stats.h"
#include "storage.h"
#include "http_routes.h"
#include "auth.h"

AsyncWebServer server(HTTP_PORT);

// -- 500 Hz hardware-timer ISR ------------------------------------------------
hw_timer_t* gTimer = nullptr;

void IRAM_ATTR onSampleTick() {
  gNewSample = true;
}

static void start_sample_timer() {
  gTimer = timerBegin(1000000);                          // 1 MHz tick
  if (!gTimer) { Serial.println("[Timer] alloc FAILED"); return; }
  timerAttachInterrupt(gTimer, &onSampleTick);
  timerAlarm(gTimer, SAMPLE_PERIOD_US, true, 0);
}

// -- Standby + foot-press / motion wake ---------------------------------------
//
// The housing seals over the on-board BOOT button, which means a true deep
// sleep with GPIO9 as the wake source would brick the device until someone
// pried the case open. The ESP32-C3 only exposes GPIO0-5 as RTC-capable
// pins, and all of ours are tied up (3x shared ADCs + 2x I2C), so we
// can't tie the MPU-6050 INT line to a deep-sleep wake pin either.
//
// Workaround: light sleep with a 1.5 s RTC timer. On each wake we briefly
// power one FSR set, take three ADC reads, then check the IMU acceleration
// magnitude (belt-and-suspenders -- either gesture wakes us). If anything
// triggers -> full wake. If not -> back to sleep. Total active time per
// cycle is ~900 us out of 1.5 s = ~0.06% duty cycle, sitting around the
// ~150 uA light-sleep floor of the C3.
//
// We also tear WiFi down before entering the loop to save the ~30 mA the
// soft-AP otherwise draws continuously, and bring it back up after wake.
//
// On wake, gWakeGraceExpiresMs is set to (now + WAKE_GRACE_MS). loop() then
// re-enters standby if no phone connects within that window -- this catches
// "the insole got squished in your gym bag" false wakes without leaving
// the radio on indefinitely.
static const uint32_t WAKE_GRACE_MS = 60000;   // 60s to connect before back to standby
static volatile uint32_t sWakeGraceExpiresMs = 0;

// Brief IMU accel-magnitude read for the wake-check loop. Returns the
// magnitude in m/s^2 (or NAN if the I2C transaction failed). At rest the
// magnitude reads ~9.81; any nontrivial motion shifts it.
static float read_imu_accel_magnitude() {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x3B);                                 // ACCEL_XOUT_H
  if (Wire.endTransmission(false) != 0) return NAN;
  if (Wire.requestFrom((uint8_t)MPU6050_ADDR, (uint8_t)6) != 6) return NAN;
  int16_t rax = (Wire.read() << 8) | Wire.read();
  int16_t ray = (Wire.read() << 8) | Wire.read();
  int16_t raz = (Wire.read() << 8) | Wire.read();
  float ax = (rax / ACCEL_LSB_PER_G) * G_TO_MS2;
  float ay = (ray / ACCEL_LSB_PER_G) * G_TO_MS2;
  float az = (raz / ACCEL_LSB_PER_G) * G_TO_MS2;
  return sqrtf(ax*ax + ay*ay + az*az);
}

static void enter_foot_press_standby() {
  Serial.println("[Standby] wake armed -- step on insole OR pick it up to wake");
  Serial.flush();

  // Drain any in-flight HTTP requests before tearing the WiFi stack down --
  // same race that bit us with the old deep-sleep teardown.
  delay(500);

  // Park both FSR sets LOW (defensive -- avoids any phantom current paths
  // while the chip is asleep).
  pinMode(PIN_PWR_SET1, OUTPUT);
  pinMode(PIN_PWR_SET2, OUTPUT);
  digitalWrite(PIN_PWR_SET1, LOW);
  digitalWrite(PIN_PWR_SET2, LOW);

  // Drop the WiFi AP so the radio stops drawing ~30 mA. Light sleep alone
  // would only get us "modem sleep" which still costs ~15 mA average.
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);

  // Wake-detection loop. esp_light_sleep_start() preserves RAM and CPU
  // state, so when we wake from the timer we just continue executing
  // the next instruction.
  const uint32_t SLEEP_US                = 1500000;  // 1.5 s between checks
  const int      WAKE_FSR_THRESHOLD      = 250;       // ADC counts above unloaded baseline
  const float    WAKE_MOTION_DELTA_MS2   = 2.0f;      // |accel| - 1g threshold (~0.2g)
  uint32_t       cycle_count             = 0;
  const char*    wake_cause              = "?";

  while (true) {
    esp_sleep_enable_timer_wakeup(SLEEP_US);
    esp_light_sleep_start();
    cycle_count++;

    // -- 1. Foot-press check (Set 1) ------------------------------------
    digitalWrite(PIN_PWR_SET1, HIGH);
    delayMicroseconds(200);
    int a = analogRead(PIN_ADC_A);
    int b = analogRead(PIN_ADC_B);
    int c = analogRead(PIN_ADC_C);
    digitalWrite(PIN_PWR_SET1, LOW);
    int max_a = a > b ? (a > c ? a : c) : (b > c ? b : c);
    if (max_a > WAKE_FSR_THRESHOLD) { wake_cause = "foot-press (set1)"; break; }

    // -- 2. Foot-press check (Set 2) ------------------------------------
    digitalWrite(PIN_PWR_SET2, HIGH);
    delayMicroseconds(200);
    int d = analogRead(PIN_ADC_A);
    int e = analogRead(PIN_ADC_B);
    int f = analogRead(PIN_ADC_C);
    digitalWrite(PIN_PWR_SET2, LOW);
    int max_b = d > e ? (d > f ? d : f) : (e > f ? e : f);
    if (max_b > WAKE_FSR_THRESHOLD) { wake_cause = "foot-press (set2)"; break; }

    // -- 3. IMU motion check --------------------------------------------
    // At rest the accel magnitude reads ~9.81 (gravity only). Picking the
    // insole up, shaking it, or any nontrivial movement shifts it. The
    // ~0.2g delta threshold is loose enough to catch a deliberate "pick
    // it up" gesture and tight enough to ignore bench-vibration noise.
    float mag = read_imu_accel_magnitude();
    if (!isnan(mag) && fabsf(mag - G_TO_MS2) > WAKE_MOTION_DELTA_MS2) {
      wake_cause = "motion (IMU)";
      break;
    }
  }

  Serial.printf("[Wake] cause: %s, cycles=%u (~%.1fs of standby)\n",
                wake_cause, (unsigned)cycle_count,
                cycle_count * (SLEEP_US / 1000000.0f));

  // Bring the WiFi AP back up. mDNS has to be re-bound since its task tied
  // to the old WiFi event-loop state.
  WiFi.mode(WIFI_AP);
  WiFi.softAP(SS_AP_SSID, SS_AP_PASS);
  IPAddress ip = WiFi.softAPIP();
  Serial.printf("[Wake] WiFi AP '%s' restored at %s\n", SS_AP_SSID, ip.toString().c_str());

  MDNS.end();
  if (MDNS.begin("solesense")) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("[Wake] mDNS solesense.local rebound");
  }

  // Arm the wake-grace window. If no phone connects within 60s of this
  // moment, loop() will trigger another standby entry (false-wake handler).
  sWakeGraceExpiresMs = millis() + WAKE_GRACE_MS;
}

// Called from loop() each tick after a wake. If a phone connects we cancel
// the grace timer; if it expires with no connection, we go back to standby.
static void check_wake_grace_window() {
  if (sWakeGraceExpiresMs == 0) return;            // grace not armed
  uint32_t now = millis();
  uint8_t  clients = WiFi.softAPgetStationNum();
  if (clients > 0) {
    // Phone connected -- the wake was intentional. Cancel grace.
    Serial.println("[Wake] client connected within grace, staying up");
    sWakeGraceExpiresMs = 0;
  } else if ((int32_t)(now - sWakeGraceExpiresMs) >= 0) {
    // Grace expired with nobody home. Most likely a false wake from the
    // insole rattling in a bag. Re-enter standby instead of burning the
    // radio for nothing.
    Serial.println("[Standby] no connection within grace window -- back to standby");
    sWakeGraceExpiresMs = 0;
    gSleepRequested = true;                         // serviced on next loop tick
  }
}

// Per-sample processing. Vertical IMU jerk and FSR-jerk are tracked as
// fusion signals alongside the FSR-based step detector. The "has prev"
// flags must be cleared whenever sampling pauses and resumes (Wi-Fi
// drop, iOS captive-portal probe, etc.) -- otherwise the next call
// computes (now - prev) over a seconds-long gap and produces a giant
// spurious jerk that falsely fires the `high_loading` injury flag.
// state.cpp sets gResetSampleTracking on resume; we honor it here.
static float sPrevAccelZ  = 0.0f;
static bool  sJerkHasPrev = false;
static float sPrevAny     = 0.0f;
static bool  sAnyHasPrev  = false;

static void process_sample() {
  sensors_read_all();
  gSampleCount++;

  if (gResetSampleTracking) {
    sJerkHasPrev = false;
    sAnyHasPrev  = false;
    gResetSampleTracking = false;
  }

  // Vertical jerk: |accel_z| / t over the last sample interval.
  if (sJerkHasPrev) {
    constexpr float DT_S = 1.0f / (float)SAMPLE_RATE_HZ;
    float jerk = fabsf(gAccel[2] - sPrevAccelZ) / DT_S;
    if (jerk > gMaxJerkZ) gMaxJerkZ = jerk;
  }
  sPrevAccelZ  = gAccel[2];
  sJerkHasPrev = true;

  // Stack-allocating a 12-element float every 2 ms hits the cache, but it
  // also re-zeros on every call -- avoid by making it static-local.
  static float channelVal[N_CHANNELS_TOTAL];
  for (uint8_t i = 0; i < N_FSR; i++) channelVal[i] = (float)gFsr[i];
  channelVal[N_FSR + 0] = gAccel[0];
  channelVal[N_FSR + 1] = gAccel[1];
  channelVal[N_FSR + 2] = gAccel[2];
  channelVal[N_FSR + 3] = gGyro[0];
  channelVal[N_FSR + 4] = gGyro[1];
  channelVal[N_FSR + 5] = gGyro[2];

  float az_mean = 0.0f, az_stddev = 0.0f;
  for (uint8_t c = 0; c < N_CHANNELS_TOTAL; c++) {
    stats_update(c, channelVal[c]);
    float mean = stats_get_mean(c);
    float std  = stats_get_stddev(c);
    if (c == N_FSR + 2) { az_mean = mean; az_stddev = std; }

    // Outlier-first: a sample over the sigma threshold is captured to the
    // ring buffer but skipped from the FFT so impact spikes don't smear it.
    bool isOutlier = outliers_offer(gRunElapsedMs, c, channelVal[c], mean, std);
    if (!isOutlier) {
      fft_process_sample(c, channelVal[c], mean);
    }
  }

  // IMU sensor-fusion: connected iff accel_z has accumulated noise (a
  // disconnected MPU-6050 leaves gAccel pinned, so stddev stays exactly 0).
  float az = channelVal[N_FSR + 2];
  if (az_stddev > IMU_CONNECTED_STDDEV_FLOOR) gImuConnected = true;
  if (gImuConnected && fabsf(az - az_mean) > IMU_IMPACT_DELTA_MS2) {
    gLastImuImpactMs = gRunElapsedMs;
    gImuImpactCount++;
  }

  // Single FSR pass: total pressure for diagnostics, OR-gate max for the
  // step detector. Refractory window in state.cpp keeps a single stride from
  // counting heel->midfoot->forefoot as three strikes.
  float totalPressure = 0.0f;
  float anyZoneMax    = channelVal[0];
  for (uint8_t i = 0; i < N_FSR; i++) {
    totalPressure += channelVal[i];
    if (channelVal[i] > anyZoneMax) anyZoneMax = channelVal[i];
  }
  if (totalPressure > gMaxTotalPressure) gMaxTotalPressure = totalPressure;
  step_detector_update(anyZoneMax,
                       stats_get_mean(0),
                       stats_get_stddev(0),
                       gRunElapsedMs);

  // FSR-jerk loading rate: track per-sample d(anyZoneMax)/dt and keep the
  // peak. Same OR-gate: peak rate-of-rise wherever the impact lands counts.
  // sPrevAny/sAnyHasPrev are file-scope so they reset on pause/resume.
  if (sAnyHasPrev) {
    float jerk = (anyZoneMax - sPrevAny) * (float)SAMPLE_RATE_HZ;
    if (jerk > gMaxHeelJerk) gMaxHeelJerk = jerk;
  }
  sPrevAny    = anyZoneMax;
  sAnyHasPrev = true;
}

// -- setup() ------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("=== SoleSense v0.2 booting ===");

  if (!LittleFS.begin()) {
    Serial.println("[FS] mount FAILED");
  } else {
    Serial.printf("[FS] Mounted - %u / %u bytes used\n",
                  (unsigned)LittleFS.usedBytes(), (unsigned)LittleFS.totalBytes());
  }

  pinMode(PIN_WAKE, INPUT_PULLUP);
  sensors_init();
  fft_init();
  outliers_reset();
  stats_reset();
  storage_init();
  state_init();
  auth_init();

  WiFi.softAP(SS_AP_SSID, SS_AP_PASS);
  IPAddress ip = WiFi.softAPIP();
  Serial.printf("[WiFi] AP '%s' up at %s\n", SS_AP_SSID, ip.toString().c_str());

  // mDNS: lets users on iOS/macOS visit http://solesense.local/ instead
  // of typing the IP. Doesn't trigger any captive-portal auto-popup --
  // it's a passive hostname resolver. Android Chrome historically doesn't
  // support .local; those users type 192.168.4.1 directly.
  if (MDNS.begin("solesense")) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("[mDNS] solesense.local resolving");
  } else {
    Serial.println("[mDNS] init FAILED (continuing without)");
  }

  http_register_routes(server);
  server.begin();
  Serial.println("[HTTP] server started");

  start_sample_timer();
  Serial.printf("[Timer] %u Hz sampling armed\n", (unsigned)SAMPLE_RATE_HZ);
}

// -- loop() -------------------------------------------------------------------
void loop() {
  state_tick();
  auth_serial_console_tick();   // listen for "factory_reset" from USB serial

  if (gSleepRequested) {
    gSleepRequested = false;
    enter_foot_press_standby();
    // Returns here on wake (light sleep preserves stack + RAM, unlike
    // the old deep_sleep flow). WiFi AP is back up; loop continues.
    return;
  }

  // Auto-standby on false wake: if we just came back from standby and
  // no phone has connected within the grace window, go back to sleep.
  check_wake_grace_window();

  if (gRunActive && gNewSample) {
    gNewSample = false;
    process_sample();
  }

  delay(1);
}
