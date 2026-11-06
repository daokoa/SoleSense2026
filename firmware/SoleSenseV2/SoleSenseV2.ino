// SoleSense v0.2 main sketch.
// Architecture: docs/design/specs/2026-05-06-v0.2-data-architecture.md

#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <esp_sleep.h>
#include <esp_wifi.h>   // esp_wifi_set_ps, esp_wifi_set_max_tx_power — single-cell power tuning
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
  //
  // *** DIAGNOSTIC MODE *** — threshold lowered + per-cycle ADC dump so
  // we can see exactly what the chip reads when you press a pad. Restore
  // WAKE_FSR_THRESHOLD = 250 and remove the Serial.printf below once we
  // know the real numbers.
  const uint32_t SLEEP_US                = 1500000;  // 1.5 s between checks
  const int      WAKE_FSR_THRESHOLD      = 80;        // DIAG: was 250
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

    // -- 2. Foot-press check (Set 2) ------------------------------------
    digitalWrite(PIN_PWR_SET2, HIGH);
    delayMicroseconds(200);
    int d = analogRead(PIN_ADC_A);
    int e = analogRead(PIN_ADC_B);
    int f = analogRead(PIN_ADC_C);
    digitalWrite(PIN_PWR_SET2, LOW);
    int max_b = d > e ? (d > f ? d : f) : (e > f ? e : f);

    // -- 3. IMU motion check --------------------------------------------
    float mag = read_imu_accel_magnitude();

    // DIAGNOSTIC: dump everything we just read so the serial monitor
    // shows what the chip sees on each cycle. Always prints, no break-cond.
    Serial.printf("[Diag] cycle=%u  set1=[%d,%d,%d] max=%d  set2=[%d,%d,%d] max=%d  |g|=%.2f (delta %.2f)\n",
      (unsigned)cycle_count, a, b, c, max_a, d, e, f, max_b,
      isnan(mag) ? -1.0f : mag,
      isnan(mag) ? -1.0f : fabsf(mag - G_TO_MS2));
    Serial.flush();

    if (max_a > WAKE_FSR_THRESHOLD) { wake_cause = "foot-press (set1)"; break; }
    if (max_b > WAKE_FSR_THRESHOLD) { wake_cause = "foot-press (set2)"; break; }
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
// Total-pressure contact tracker: gated on the SUM of all 6 FSRs against a
// fixed noise floor. Step detector's Kalman-velocity contact gate drops out
// when the *loudest* FSR's velocity falls, which underreports ground time
// for forefoot strikers and pivots. Summing across all sensors stays high
// for the entire foot-down interval.
static bool     sFootDown        = false;
static uint32_t sFootDownStartMs = 0;
static bool     sFftCoefsRefit   = false;

static void process_sample() {
  sensors_read_all();
  gSampleCount++;

  if (gResetSampleTracking) {
    sJerkHasPrev = false;
    sAnyHasPrev  = false;
    sFootDown    = false;
    gResetSampleTracking = false;
  }

  // Periodically refresh the measured sample rate. The bool gNewSample
  // flag coalesces ISR fires the loop can't drain (loop iteration > 2 ms),
  // so the effective rate is typically ~250 Hz, not the nominal 500. Every
  // 256 samples (~1 s at real rate) recompute Fs from the wall clock so
  // jerk + Kalman use the right dt. Once we have a stable measurement, also
  // refit the Goertzel coefficients so FFT bin labels match reality.
  if ((gSampleCount & 0xFF) == 0 && gRunElapsedMs > 500UL) {
    gEffectiveSampleRate = (float)gSampleCount * 1000.0f / (float)gRunElapsedMs;
    if (!sFftCoefsRefit && gSampleCount >= 512) {
      fft_recompute_coefs(gEffectiveSampleRate);
      sFftCoefsRefit = true;
      Serial.printf("[FFT] coefs refit to %.1f Hz (after %lu samples)\n",
                    gEffectiveSampleRate, (unsigned long)gSampleCount);
    }
  }

  // Vertical jerk: |accel_z| / t over the last sample interval. dt comes
  // from the measured rate, not the nominal config — see comment above.
  if (sJerkHasPrev) {
    const float DT_S = (gEffectiveSampleRate > 1.0f)
                         ? 1.0f / gEffectiveSampleRate
                         : 1.0f / (float)SAMPLE_RATE_HZ;
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

  // Total-foot contact tracker (decoupled from the step counter). Foot is
  // "down" while the SUM across all 6 FSRs sits above a generous noise
  // floor (~100 ADC counts per sensor average). Interval start→end is the
  // true ground-contact time, regardless of strike pattern. Floor 600 was
  // chosen to clear EMA settling + ADC quiescent noise but admit even
  // soft forefoot landings (forefoot pads alone clear 1000+ on contact).
  constexpr float TOTAL_PRESSURE_FLOOR  = 600.0f;
  constexpr uint32_t CONTACT_MIN_VALID  = 40;
  constexpr uint32_t CONTACT_MAX_VALID  = 1500;
  if (totalPressure > TOTAL_PRESSURE_FLOOR) {
    if (!sFootDown) {
      sFootDown        = true;
      sFootDownStartMs = gRunElapsedMs;
    }
  } else if (sFootDown) {
    sFootDown = false;
    uint32_t contact = gRunElapsedMs - sFootDownStartMs;
    if (contact >= CONTACT_MIN_VALID && contact <= CONTACT_MAX_VALID) {
      gContactSumMs += contact;
      gContactCount++;
    }
  }

  // FSR-jerk loading rate: track per-sample d(anyZoneMax)/dt and keep the
  // peak. Multiplied by the *measured* Fs so the BW/s number is honest;
  // the old constant SAMPLE_RATE_HZ over-reported jerk by ~2× at the
  // actual ~250 Hz effective rate. sPrevAny/sAnyHasPrev are file-scope so
  // they reset on pause/resume.
  if (sAnyHasPrev) {
    const float fs = (gEffectiveSampleRate > 1.0f) ? gEffectiveSampleRate
                                                   : (float)SAMPLE_RATE_HZ;
    float jerk = (anyZoneMax - sPrevAny) * fs;
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
  // Reset-reason diagnostic. ESP_RST_BROWNOUT (9) here = the cell sagged
  // below the LDO dropout under load on the previous boot. ESP_RST_POWERON
  // (1) = clean start. Anything else (PANIC 4, WDT 5/6/7) = firmware bug.
  esp_reset_reason_t reset_reason = esp_reset_reason();
  const char* rr_name =
      (reset_reason == ESP_RST_POWERON)   ? "POWERON" :
      (reset_reason == ESP_RST_EXT)       ? "EXT_PIN" :
      (reset_reason == ESP_RST_SW)        ? "SW_RESTART" :
      (reset_reason == ESP_RST_PANIC)     ? "PANIC_EXC" :
      (reset_reason == ESP_RST_INT_WDT)   ? "WDT_INT" :
      (reset_reason == ESP_RST_TASK_WDT)  ? "WDT_TASK" :
      (reset_reason == ESP_RST_WDT)       ? "WDT_OTHER" :
      (reset_reason == ESP_RST_DEEPSLEEP) ? "DEEPSLEEP" :
      (reset_reason == ESP_RST_BROWNOUT)  ? "BROWNOUT  <-- cell sag, single-LIR2450 cannot sustain load" :
      (reset_reason == ESP_RST_SDIO)      ? "SDIO" :
                                            "UNKNOWN";
  Serial.printf("[Reset] reason=%d (%s)\n", (int)reset_reason, rr_name);

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

  // Crash/power-cut recovery: rebuild the last run's report inputs into RAM
  // (aggregates + Welford stats + FFT magnitudes) so /api/run-report serves
  // the last run instead of zeros if the device lost power before the report
  // was fetched. Must run AFTER state_init() (which zeroes the run timer).
  if (storage_restore_latest()) {
    Serial.println("[Boot] restored last run from flash -- /api/run-report ready");
  }

  WiFi.softAP(SS_AP_SSID, SS_AP_PASS);
  IPAddress ip = WiFi.softAPIP();
  Serial.printf("[WiFi] AP '%s' up at %s\n", SS_AP_SSID, ip.toString().c_str());

  // DEMO POWER PROFILE: max TX power, modem-sleep OFF. Running on USB-C
  // power bank for the pitch, so the single-cell sag that forced the
  // 8.5 dBm + MIN_MODEM compromise isn't a constraint. ~19.5 dBm + no
  // modem-sleep gives full ~50 m range, fastest beacon cadence, and the
  // shortest phone-association latency. Peak current ~300-400 mA, but
  // the power bank handles it without sagging the rails.
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  esp_wifi_set_ps(WIFI_PS_NONE);
  Serial.println("[WiFi] DEMO profile: TX=19.5dBm, modem-sleep OFF (USB power bank)");

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

  // DEMO BUILD: post-boot auto-standby DISABLED. Wake from light-sleep was
  // unreliable on this hardware (USB-CDC didn't re-enumerate post-wake and
  // wake-trigger never fired during testing), so the device now stays
  // awake from boot to power loss. AP stays up the whole time; ~72 min
  // runtime on a single LIR2450. The /api/sleep endpoint + UI Sleep
  // button still exist but should NOT be tapped during the demo --
  // tapping bricks the device until USB hardware reset.
  // To re-enable: uncomment the two lines below.
  // sWakeGraceExpiresMs = millis() + WAKE_GRACE_MS;
  // Serial.println("[Boot] post-boot grace armed -- 60s to first connection or standby");
  Serial.println("[Boot] DEMO BUILD: auto-standby disabled, AP stays up indefinitely");
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
