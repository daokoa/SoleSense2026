// =============================================================================
// SoleSense v0.2 — http_routes.cpp
//
// Simple state-changing routes (start, stop, calibrate, sleep, device, sensor)
// are fully working. The new v0.2 endpoints (run-state, run-spectrum,
// run-outliers, run-report) return shape-correct JSON populated from the FFT
// and outlier modules. /api/run-report is the heaviest — it computes the
// final injury-flag analysis from the FFT bins and outliers.
//
// **STATUS: simple routes done; analysis logic in /api/run-report is a stub.**
// =============================================================================

#include "http_routes.h"
#include "config.h"
#include "state.h"
#include "sensors.h"
#include "fft.h"
#include "outliers.h"
#include "stats.h"
#include "storage.h"

#include <LittleFS.h>
#include <WiFi.h>
#include <math.h>

// ── /api/device ──────────────────────────────────────────────────────────────
static void handle_device(AsyncWebServerRequest* req) {
  String j = "{";
  j += "\"firmware\":\"SoleSense ";  j += SS_FIRMWARE_VERSION; j += "\",";
  j += "\"version\":\"";              j += SS_FIRMWARE_VERSION; j += "\",";
  j += "\"board\":\"";                j += SS_BOARD_NAME;       j += "\",";
  j += "\"sampleRateHz\":";           j += SAMPLE_RATE_HZ;      j += ",";
  j += "\"heap_free\":";              j += (unsigned)ESP.getFreeHeap(); j += ",";
  j += "\"state\":\"";                j += state_name();        j += "\",";
  j += "\"fs\":{\"totalBytes\":";     j += (unsigned)LittleFS.totalBytes();
  j += ",\"usedBytes\":";             j += (unsigned)LittleFS.usedBytes(); j += "},";
  j += "\"thresholds_hardcoded\":true";
  j += "}";
  req->send(200, "application/json", j);
}

// ── /api/sensor ──────────────────────────────────────────────────────────────
static void handle_sensor(AsyncWebServerRequest* req) {
  // Only re-read when idle (during a recording, the sample loop keeps
  // gFsr/gAccel/gGyro fresh at SAMPLE_RATE_HZ).
  if (gState == RS_IDLE) sensors_read_all();
  char body[256];
  snprintf(body, sizeof(body),
    "{\"ax\":%.3f,\"ay\":%.3f,\"az\":%.3f,"
    "\"gx\":%.3f,\"gy\":%.3f,\"gz\":%.3f,"
    "\"fsr\":[%d,%d,%d,%d,%d,%d]}",
    gAccel[0], gAccel[1], gAccel[2],
    gGyro[0],  gGyro[1],  gGyro[2],
    gFsr[0], gFsr[1], gFsr[2], gFsr[3], gFsr[4], gFsr[5]);
  req->send(200, "application/json", body);
}

// ── /api/start /api/stop /api/sleep ──────────────────────────────────────────
static void handle_start(AsyncWebServerRequest* req) {
  if (gState != RS_IDLE) {
    req->send(409, "application/json", "{\"ok\":false,\"error\":\"already recording\"}");
    return;
  }
  gStartRequested = true;
  req->send(200, "application/json", "{\"ok\":true}");
}

static void handle_stop(AsyncWebServerRequest* req) {
  if (gState != RS_RECORDING) {
    req->send(409, "application/json", "{\"ok\":false,\"error\":\"not recording\"}");
    return;
  }
  gStopRequested = true;
  req->send(200, "application/json", "{\"ok\":true}");
}

static void handle_sleep(AsyncWebServerRequest* req) {
  if (gState == RS_RECORDING) {
    req->send(409, "application/json", "{\"ok\":false,\"error\":\"recording\"}");
    return;
  }
  gSleepRequested = true;
  req->send(200, "application/json", "{\"ok\":true}");
}

// ── /api/calibrate/zero /api/calibrate/imu ───────────────────────────────────
static void handle_cal_zero(AsyncWebServerRequest* req) {
  if (gState != RS_IDLE) {
    req->send(409, "application/json", "{\"ok\":false,\"error\":\"recording\"}");
    return;
  }
  sensors_calibrate_fsr();
  String j = "{\"ok\":true,\"fsrZero\":[";
  for (int i = 0; i < N_FSR; i++) { j += gFsrZero[i]; if (i < N_FSR-1) j += ","; }
  j += "]}";
  req->send(200, "application/json", j);
}

static void handle_cal_imu(AsyncWebServerRequest* req) {
  if (gState != RS_IDLE) {
    req->send(409, "application/json", "{\"ok\":false,\"error\":\"recording\"}");
    return;
  }
  sensors_calibrate_imu();
  char body[200];
  snprintf(body, sizeof(body),
    "{\"ok\":true,\"accel\":[%.4f,%.4f,%.4f],\"gyro\":[%.4f,%.4f,%.4f]}",
    gImuOffset[0],gImuOffset[1],gImuOffset[2],
    gImuOffset[3],gImuOffset[4],gImuOffset[5]);
  req->send(200, "application/json", body);
}

// ── /api/run-state ───────────────────────────────────────────────────────────
static void handle_run_state(AsyncWebServerRequest* req) {
  String j = "{";
  j += "\"recording\":";          j += (gState == RS_RECORDING ? "true" : "false"); j += ",";
  j += "\"run_active\":";         j += (gRunActive ? "true" : "false");             j += ",";
  j += "\"elapsed_ms\":";         j += (unsigned long)gRunElapsedMs;                j += ",";
  j += "\"sample_count\":";       j += (unsigned long)gSampleCount;                 j += ",";
  j += "\"clients_connected\":";  j += (unsigned)WiFi.softAPgetStationNum();        j += ",";
  j += "\"outlier_count\":";      j += (unsigned)outliers_count();
  j += "}";
  req->send(200, "application/json", j);
}

// ── /api/run-spectrum ────────────────────────────────────────────────────────
static void handle_run_spectrum(AsyncWebServerRequest* req) {
  // Returns the FFT magnitudes per (channel, bin). Compact format:
  //   { "binsHz":[...], "channels":[{"name":"heel","mag":[...]}, ...] }
  String j = "{\"binsHz\":[";
  for (uint8_t b = 0; b < FFT_BINS_PER_CHAN; b++) {
    j += FFT_BIN_FREQS_HZ[b]; if (b < FFT_BINS_PER_CHAN-1) j += ",";
  }
  j += "],\"channels\":[";
  static const char* CH_NAMES[N_CHANNELS_TOTAL] = {
    "heel","lat_mid","med_mid","ball_lat","ball_med","toe",
    "ax","ay","az","gx","gy","gz"
  };
  for (uint8_t c = 0; c < N_CHANNELS_TOTAL; c++) {
    j += "{\"name\":\""; j += CH_NAMES[c]; j += "\",\"mag\":[";
    for (uint8_t b = 0; b < FFT_BINS_PER_CHAN; b++) {
      j += String(fft_get_magnitude(c, b), 4);
      if (b < FFT_BINS_PER_CHAN-1) j += ",";
    }
    j += "]}";
    if (c < N_CHANNELS_TOTAL-1) j += ",";
  }
  j += "]}";
  req->send(200, "application/json", j);
}

// ── /api/run-outliers ────────────────────────────────────────────────────────
static void handle_run_outliers(AsyncWebServerRequest* req) {
  String j = "[";
  for (uint8_t i = 0; i < outliers_count(); i++) {
    const Outlier& o = outliers_at(i);
    j += "{\"ts\":";        j += (unsigned long)o.timestamp_ms;
    j += ",\"channel\":";   j += o.channel;
    j += ",\"value\":";     j += String(o.value, 3);
    j += ",\"sigma\":";     j += String(o.sigma, 2);
    j += "}";
    if (i < outliers_count()-1) j += ",";
  }
  j += "]";
  req->send(200, "application/json", j);
}

// ── /api/run-report ──────────────────────────────────────────────────────────
// Computes the final metrics from the FFT bins + outlier buffer + running
// channel stats. Mirrors the v0.1 dao analyse() function but pulls data from
// the device-side modules instead of parsed CSV rows.
//
// Channel index reminder:
//   0..5   = FSRs (heel, lat-mid, med-mid, ball-lat, ball-med, toe-1)
//   6..8   = accel x/y/z
//   9..11  = gyro x/y/z
static void handle_run_report(AsyncWebServerRequest* req) {
  // ── Cadence: peak FFT bin in the heel channel within the 1–4 Hz stride band.
  // Require BOTH a meaningful magnitude AND a minimum number of heel-channel
  // outliers (= detected impacts). A pure-noise window can occasionally cross
  // a magnitude threshold; it cannot also produce N>=5 σ-outlier events. The
  // double gate kills the "67 / 100 fake steps with nothing pressed" bug.
  constexpr float CADENCE_MIN_MAG       = 50.0f;
  constexpr uint8_t CADENCE_MIN_OUTLIERS = 5;

  uint8_t cadenceBin = 0;
  float   cadenceMag = 0.0f;
  for (uint8_t b = 0; b < FFT_BINS_PER_CHAN; b++) {
    float f = FFT_BIN_FREQS_HZ[b];
    if (f < 1.0f || f > 4.0f) continue;
    float m = fft_get_magnitude(0, b);   // channel 0 = heel
    if (m > cadenceMag) { cadenceMag = m; cadenceBin = b; }
  }

  uint8_t heelOutliers = 0;
  for (uint8_t i = 0; i < outliers_count(); i++) {
    if (outliers_at(i).channel == 0) heelOutliers++;
  }

  bool cadenceValid = (cadenceMag > CADENCE_MIN_MAG)
                   && (heelOutliers >= CADENCE_MIN_OUTLIERS);
  float strideHz = cadenceValid ? FFT_BIN_FREQS_HZ[cadenceBin] : 0.0f;
  int   cadence  = (int)(strideHz * 60.0f);

  // ── Zone means (raw FSR units; the frontend percentage-ifies for display)
  float zHeel    = stats_get_mean(0);
  float zMidfoot = (stats_get_mean(1) + stats_get_mean(2)) * 0.5f;
  float zBall    = (stats_get_mean(3) + stats_get_mean(4)) * 0.5f;
  float zToe     = stats_get_mean(5);

  // ── Medial vs lateral on a single insole.
  // This is NOT left-foot vs right-foot — the system has one insole. The split
  // is medial (inside of the foot) vs lateral (outside of the foot) loading,
  // averaging the relevant FSR channels.
  float medial  = (stats_get_mean(2) + stats_get_mean(4) + stats_get_mean(5)) / 3.0f;
  float lateral = (stats_get_mean(1) + stats_get_mean(3)) * 0.5f;
  float mlTotal = medial + lateral;
  float medialPct  = mlTotal > 0.0f ? medial  / mlTotal * 100.0f : 50.0f;
  float lateralPct = mlTotal > 0.0f ? lateral / mlTotal * 100.0f : 50.0f;
  float asymPct    = fabsf(medialPct - lateralPct);

  // ── Heel-vs-forefoot strike ratio
  float foreLoad  = (zBall + zToe) * 0.5f;
  float hfTotal   = zHeel + foreLoad;
  float heelRatio = hfTotal > 0.0f ? zHeel / hfTotal * 100.0f : 50.0f;

  // ── Peak loading rate from IMU jerk (BW/s).
  // FSR 402 caps at ~10 kg, so direct force measurement isn't possible during
  // running impacts (100–200 kg of ground-reaction force). Instead we use the
  // vertical jerk: dividing peak |d(accel_z)/dt| by g (9.81 m/s²) gives a
  // value with units of 1/s ≈ body-weights-per-second, the standard
  // biomechanics loading-rate metric. Healthy runners read 30–80 BW/s; >80
  // is associated with stress-fracture / shin-splint risk (Milner 2006).
  // Returns 0 when the IMU isn't connected (gAccel[2] doesn't change → jerk = 0).
  float loadingRateBWs = gMaxJerkZ / G_TO_MS2;

  // ── Pronation: running mean of gyro_x (degrees/s).
  // Net mean ≈ 0 for symmetric gait; positive = pronation, negative = supination.
  // First-order only. Reports 0 if the IMU isn't connected (Welford mean is 0).
  float pronate = stats_get_mean(N_FSR + 3);   // channel 9 = gyro_x

  // ── Ground contact time
  // Real GCT requires per-stride heel-strike-to-toe-off detection in the time
  // domain, which v0.2 doesn't keep (we only store the FFT spectrum + outliers).
  // The "35% of stride period" proxy was misleading; better to report 0/null
  // than a fabricated value.
  float contactMs = 0.0f;

  // ── Step count (from cadence × duration)
  uint32_t durMs  = gRunElapsedMs;
  uint32_t durSec = durMs / 1000UL;
  int      steps  = strideHz > 0.0f ? (int)(strideHz * (float)durSec) : 0;

  // ── Injury flags (same thresholds as v0.1 dao THRESH constants)
  String flags = "[";
  bool firstFlag = true;
  auto pushFlag = [&](const char* key, const String& val) {
    if (!firstFlag) flags += ",";
    flags += "{\"key\":\"";
    flags += key;
    flags += "\",\"val\":\"";
    flags += val;
    flags += "\"}";
    firstFlag = false;
  };

  if (heelRatio > 65.0f && zHeel > zBall + 10.0f) {
    pushFlag("heel_strike", String((int)heelRatio) + "% heel load");
  }
  // High-loading flag: > 80 BW/s. Threshold from biomechanics literature
  // (Milner 2006; Davis 2016): runners above this have ~2× the stress-fracture
  // risk vs. runners with loading rates < 60 BW/s.
  if (loadingRateBWs > 80.0f) {
    pushFlag("high_loading", String((int)loadingRateBWs) + " BW/s");
  }
  if (cadence > 0 && cadence < 160) {
    pushFlag("low_cadence", String(cadence) + " steps/min");
  }
  if (pronate > 15.0f) {
    pushFlag("overpronation", String(pronate, 1) + "°/s");
  } else if (pronate < -8.0f) {
    pushFlag("supination", String(fabsf(pronate), 1) + "°/s outward");
  }
  if (asymPct > 10.0f) {
    pushFlag("medial_lateral_asym",
             String((int)medialPct) + "% med / " + String((int)lateralPct) + "% lat");
  }
  // GCT flag suppressed in v0.2 — we don't measure it honestly. Re-enable
  // when time-domain step detection lands.
  flags += "]";

  // ── Build response.
  // v0.2 schema: medialPct/lateralPct (not lPct/rPct), loadingSigma (not
  // loadingRate BW/s), contactMs reports 0 when unmeasured. Frontend uses
  // these new keys and shows "—" for any value at 0.
  String j = "{";
  j += "\"steps\":";        j += steps;                     j += ",";
  j += "\"cadence\":";      j += cadence;                   j += ",";
  j += "\"durSec\":";       j += (unsigned long)durSec;     j += ",";
  j += "\"contactMs\":";    j += String(contactMs, 1);      j += ",";
  j += "\"loadingRate\":";  j += String(loadingRateBWs, 1); j += ",";
  j += "\"pronate\":";      j += String(pronate, 2);        j += ",";
  j += "\"medialPct\":";    j += String(medialPct, 1);      j += ",";
  j += "\"lateralPct\":";   j += String(lateralPct, 1);     j += ",";
  j += "\"zoneAvg\":{";
  j +=   "\"heel\":";       j += String(zHeel, 1);          j += ",";
  j +=   "\"midfoot\":";    j += String(zMidfoot, 1);       j += ",";
  j +=   "\"ball\":";       j += String(zBall, 1);          j += ",";
  j +=   "\"toe\":";        j += String(zToe, 1);
  j += "},";
  j += "\"flags\":";        j += flags;                     j += ",";
  j += "\"durationMs\":";   j += (unsigned long)durMs;      j += ",";
  j += "\"samples\":";      j += (unsigned long)gSampleCount; j += ",";
  j += "\"outliers\":";     j += (unsigned)outliers_count();
  j += "}";
  req->send(200, "application/json", j);
}

// ── Debug: /api/fft-selftest ─────────────────────────────────────────────────
// Runs the canned 2-Hz-sine validation; expects ~100 mag in the 2 Hz bin.
// Output goes to Serial Monitor; HTTP response is just an ack. Refuses while
// recording to avoid trampling live state.
static void handle_fft_selftest(AsyncWebServerRequest* req) {
  if (gState != RS_IDLE) {
    req->send(409, "application/json", "{\"ok\":false,\"error\":\"recording\"}");
    return;
  }
  fft_self_test();
  req->send(200, "application/json", "{\"ok\":true,\"see\":\"Serial Monitor\"}");
}

// ── Debug: /api/storage-selftest ─────────────────────────────────────────────
// Writes 3 slots, corrupts the newest, asserts load_latest falls back. Cleans
// up after itself. Refuses during a recording (would clobber real slots).
static void handle_storage_selftest(AsyncWebServerRequest* req) {
  if (gState != RS_IDLE) {
    req->send(409, "application/json", "{\"ok\":false,\"error\":\"recording\"}");
    return;
  }
  storage_self_test();
  req->send(200, "application/json", "{\"ok\":true,\"see\":\"Serial Monitor\"}");
}

// ── /api/storage-state ───────────────────────────────────────────────────────
// Diagnostic: how many slots are currently valid, and what's the newest header.
static void handle_storage_state(AsyncWebServerRequest* req) {
  uint8_t count = storage_valid_slot_count();
  SlotHeader hdr;
  bool has = storage_load_latest(hdr);
  String j = "{\"valid_slots\":";
  j += count;
  if (has) {
    j += ",\"latest\":{";
    j += "\"slot_n\":";        j += (unsigned long)hdr.slot_n;
    j += ",\"timestamp_ms\":"; j += (unsigned long)hdr.timestamp_ms;
    j += ",\"sample_count\":"; j += (unsigned long)hdr.sample_count;
    j += ",\"run_done\":";     j += (unsigned long)hdr.run_done;
    j += "}";
  } else {
    j += ",\"latest\":null";
  }
  j += "}";
  req->send(200, "application/json", j);
}

// ── Registration ─────────────────────────────────────────────────────────────
void http_register_routes(AsyncWebServer& server) {
  server.on("/api/device",         HTTP_GET,  handle_device);
  server.on("/api/sensor",         HTTP_GET,  handle_sensor);
  server.on("/api/start",          HTTP_POST, handle_start);
  server.on("/api/stop",           HTTP_POST, handle_stop);
  server.on("/api/sleep",          HTTP_POST, handle_sleep);
  server.on("/api/calibrate/zero", HTTP_POST, handle_cal_zero);
  server.on("/api/calibrate/imu",  HTTP_POST, handle_cal_imu);
  server.on("/api/run-state",      HTTP_GET,  handle_run_state);
  server.on("/api/run-spectrum",   HTTP_GET,  handle_run_spectrum);
  server.on("/api/run-outliers",   HTTP_GET,  handle_run_outliers);
  server.on("/api/run-report",     HTTP_GET,  handle_run_report);
  server.on("/api/fft-selftest",     HTTP_POST, handle_fft_selftest);
  server.on("/api/storage-selftest", HTTP_POST, handle_storage_selftest);
  server.on("/api/storage-state",    HTTP_GET,  handle_storage_state);
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
}
