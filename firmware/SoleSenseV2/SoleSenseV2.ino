// SoleSense v0.2 main sketch.
// Architecture: docs/design/specs/2026-05-06-v0.2-data-architecture.md

#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
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

AsyncWebServer server(HTTP_PORT);

// -- 50 Hz hardware-timer ISR -------------------------------------------------
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

// -- Deep sleep ---------------------------------------------------------------
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

// Per-sample processing. Vertical IMU jerk is tracked as a fusion signal
// alongside the FSR-based step detector.
static float sPrevAccelZ  = 0.0f;
static bool  sJerkHasPrev = false;

static void process_sample() {
  sensors_read_all();
  gSampleCount++;

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
  static float sPrevAny  = 0.0f;
  static bool  sAnyHasPrev = false;
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
    enter_deep_sleep();
    return;   // never reached
  }

  if (gRunActive && gNewSample) {
    gNewSample = false;
    process_sample();
  }

  delay(1);
}
