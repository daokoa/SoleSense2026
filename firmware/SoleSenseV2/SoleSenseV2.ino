// =============================================================================
// SoleSense v0.2 — main sketch
//
// This is the v0.2 firmware. v0.1 still lives at firmware/SoleSense/ and is
// what's flashed for the Spring 2026 demo.
//
// Architecture (full design): docs/superpowers/specs/2026-05-06-v0.2-data-architecture.md
// Implementation plan:        docs/superpowers/plans/2026-05-06-v0.2-firmware.md
// =============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <esp_sleep.h>

#include "config.h"
#include "state.h"
#include "sensors.h"
#include "fft.h"
#include "outliers.h"
#include "stats.h"
#include "storage.h"
#include "http_routes.h"
#include "auth.h"

// DNS server for captive-portal redirect — every DNS query that hits the AP
// is answered with the device's IP, so any URL the user types lands on the
// SoleSense web UI (and iOS/Android pop the captive-portal auto-launch).
static DNSServer gDns;

AsyncWebServer server(HTTP_PORT);

// ── 50 Hz hardware-timer ISR ─────────────────────────────────────────────────
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

// ── Deep sleep ───────────────────────────────────────────────────────────────
static void enter_deep_sleep() {
  Serial.println("[Sleep] entering deep sleep, wake on GPIO9 LOW");
  Serial.flush();
  delay(150);
  WiFi.softAPdisconnect(true);
  LittleFS.end();

  // Hold the wake pin high during sleep so a press-to-GND triggers wake.
  // Using the older esp-idf 4.x API that's available across Arduino-ESP32
  // core 2.x and 3.x. (The newer esp_deep_sleep_enable_gpio_wakeup is
  // 3.x-only and not present in some installations.)
  gpio_pullup_en((gpio_num_t)PIN_WAKE);
  gpio_pulldown_dis((gpio_num_t)PIN_WAKE);
  gpio_wakeup_enable((gpio_num_t)PIN_WAKE, GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  esp_deep_sleep_start();
}

// ── Per-sample processing ────────────────────────────────────────────────────
// Jerk-tracking state. We use vertical jerk (d(accel_z)/dt) as the impact-rate
// indicator because the FSR 402 saturates at ~10 kg — far below running peak
// ground-reaction force (100–200 kg). The IMU gives clean vertical
// acceleration regardless of how saturated the FSRs are, and its derivative
// is the standard biomechanics loading-rate signal.
static float sPrevAccelZ  = 0.0f;
static bool  sJerkHasPrev = false;

static void process_sample() {
  sensors_read_all();
  gSampleCount++;

  // Vertical jerk: |Δaccel_z| / Δt over the last sample interval.
  if (sJerkHasPrev) {
    constexpr float DT_S = 1.0f / (float)SAMPLE_RATE_HZ;
    float jerk = fabsf(gAccel[2] - sPrevAccelZ) / DT_S;
    if (jerk > gMaxJerkZ) gMaxJerkZ = jerk;
  }
  sPrevAccelZ  = gAccel[2];
  sJerkHasPrev = true;

  float channelVal[N_CHANNELS_TOTAL];
  for (uint8_t i = 0; i < N_FSR; i++) channelVal[i] = (float)gFsr[i];
  channelVal[N_FSR + 0] = gAccel[0];
  channelVal[N_FSR + 1] = gAccel[1];
  channelVal[N_FSR + 2] = gAccel[2];
  channelVal[N_FSR + 3] = gGyro[0];
  channelVal[N_FSR + 4] = gGyro[1];
  channelVal[N_FSR + 5] = gGyro[2];

  for (uint8_t c = 0; c < N_CHANNELS_TOTAL; c++) {
    stats_update(c, channelVal[c]);
    float mean = stats_get_mean(c);
    float std  = stats_get_stddev(c);

    // Offer to the outlier buffer first; if it bites, skip the FFT update so
    // injury-causing spikes don't smear the spectrum.
    bool isOutlier = outliers_offer(gRunElapsedMs, c, channelVal[c], mean, std);
    if (!isOutlier) {
      fft_process_sample(c, channelVal[c], mean);
    }
  }

  // ── IMU sensor-fusion bookkeeping (used by step_detector_update below).
  // Mark the IMU as "connected" once its vertical-axis stddev has grown above
  // a tiny floor (real samples have noise; disconnected IMU stays at exactly
  // zero stddev because gAccel never changes). Then detect any-axis impacts
  // by deviation from the running mean, and stamp the most recent one.
  float az        = channelVal[N_FSR + 2];   // accel_z
  float az_mean   = stats_get_mean(N_FSR + 2);
  float az_stddev = stats_get_stddev(N_FSR + 2);
  if (az_stddev > 0.05f) gImuConnected = true;
  if (gImuConnected) {
    constexpr float IMU_IMPACT_DELTA = 8.0f;   // m/s² above background
    if (fabsf(az - az_mean) > IMU_IMPACT_DELTA) {
      gLastImuImpactMs = gRunElapsedMs;
      gImuImpactCount++;
    }
  }

  // ── Total foot pressure (sum of all 6 FSR zones). Smoother signal than
  // single-channel max — useful for cross-checking the strike count and as
  // an alternative trigger if max-based detection ever proves too noisy.
  float totalPressure = 0.0f;
  for (uint8_t i = 0; i < N_FSR; i++) totalPressure += channelVal[i];
  if (totalPressure > gMaxTotalPressure) gMaxTotalPressure = totalPressure;

  // ── Time-domain step detection: OR-gate across ALL FSR zones. A real step
  // can be heel-strike, midfoot-strike, or forefoot-strike depending on the
  // runner; whichever zone makes contact first counts. The detector's own
  // refractory window (250 ms in state.cpp) prevents double-counting the
  // heel→midfoot→forefoot pressure progression within a single stride.
  // When the IMU is connected, the detector additionally requires a recent
  // IMU impact for the strike to count.
  float anyZoneMax = channelVal[0];
  for (uint8_t i = 1; i < N_FSR; i++) {
    if (channelVal[i] > anyZoneMax) anyZoneMax = channelVal[i];
  }
  step_detector_update(anyZoneMax,
                       stats_get_mean(0),
                       stats_get_stddev(0),
                       gRunElapsedMs);

  // FSR-jerk loading rate: track per-sample d(anyZoneMax)/dt and keep the
  // peak. Same OR-gate: peak rate-of-rise wherever the impact lands counts.
  static float sPrevAny  = 0.0f;
  static bool  sAnyHasPrev = false;
  if (sAnyHasPrev) {
    float jerk = (anyZoneMax - sPrevAny) * (float)SAMPLE_RATE_HZ;
    if (jerk > gMaxHeelJerk) gMaxHeelJerk = jerk;
  }
  sPrevAny    = anyZoneMax;
  sAnyHasPrev = true;
}

// ── setup() ──────────────────────────────────────────────────────────────────
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
  // of typing the IP. Android Chrome historically doesn't support .local —
  // they fall back to the captive portal pop-up below.
  if (MDNS.begin("solesense")) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("[mDNS] solesense.local resolving");
  } else {
    Serial.println("[mDNS] init FAILED (continuing without)");
  }

  // DNS captive portal: answer every query with our own IP. This makes
  // the AP look like a captive-portal hotspot, so iOS/Android automatically
  // open the SoleSense UI in a popup the moment you join the WiFi —
  // no URL typing required at all.
  gDns.start(53, "*", ip);
  Serial.println("[DNS] captive portal active (any URL → device)");

  http_register_routes(server);
  server.begin();
  Serial.println("[HTTP] server started");

  start_sample_timer();
  Serial.printf("[Timer] %u Hz sampling armed\n", (unsigned)SAMPLE_RATE_HZ);
}

// ── loop() ───────────────────────────────────────────────────────────────────
void loop() {
  state_tick();
  gDns.processNextRequest();    // serve the captive-portal DNS responses
  auth_serial_console_tick();   // listen for "factory_reset" from USB serial

  if (gSleepRequested) {
    gSleepRequested = false;
    enter_deep_sleep();
    return;   // never reached
  }

  if (gRunActive && gNewSample) {
    gNewSample = false;
    process_sample();
  }

  delay(1);
}
