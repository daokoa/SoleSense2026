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
// Channel index reminder (3-zone × medial/lateral layout, Choi 2024 +E-at-heel):
//   0 = heel medial,     1 = heel lateral
//   2 = midfoot medial,  3 = midfoot lateral
//   4 = forefoot medial, 5 = forefoot lateral   (sensors at the front, near
//                                                 the metatarsal heads /
//                                                 just behind the toe row)
//   6..8  = accel x/y/z
//   9..11 = gyro x/y/z
static void handle_run_report(AsyncWebServerRequest* req) {
  // ── Cadence + step count: time-domain heel-strike detector (state.cpp).
  // Each rising edge through max(floor, 4σ) is a step; cadence is just
  // steps × 60 / runtime. Wait for ≥2 s of recording before reporting cadence
  // so very-short-run noise doesn't produce a wild number.
  uint32_t durMs  = gRunElapsedMs;
  uint32_t durSec = durMs / 1000UL;
  int   steps   = (int)gStepCount;
  int   cadence = 0;
  if (durMs >= 2000UL && gStepCount > 0) {
    cadence = (int)((uint64_t)gStepCount * 60000ULL / (uint64_t)durMs);
  }

  // ── Zone means (raw FSR units; the frontend percentage-ifies for display).
  // Three zones × two sensors each. Clamp negatives to 0 — they only happen
  // when an FSR is unconnected and a stale calibration offset is in effect,
  // and a negative loading value isn't physically meaningful.
  auto clamp_pos = [](float v) { return v > 0.0f ? v : 0.0f; };
  float zHeel     = clamp_pos((stats_get_mean(0) + stats_get_mean(1)) * 0.5f);
  float zMidfoot  = clamp_pos((stats_get_mean(2) + stats_get_mean(3)) * 0.5f);
  float zForefoot = clamp_pos((stats_get_mean(4) + stats_get_mean(5)) * 0.5f);

  // ── Medial vs lateral on a single insole.
  // This is NOT left-foot vs right-foot — the system has one insole. The split
  // is medial (inside-of-foot) vs lateral (outside-of-foot) loading. With the
  // 3×2 layout, medial = ch{0,2,4} and lateral = ch{1,3,5}.
  float medial  = (stats_get_mean(0) + stats_get_mean(2) + stats_get_mean(4)) / 3.0f;
  float lateral = (stats_get_mean(1) + stats_get_mean(3) + stats_get_mean(5)) / 3.0f;
  float mlTotal = medial + lateral;
  float medialPct  = mlTotal > 0.0f ? medial  / mlTotal * 100.0f : 50.0f;
  float lateralPct = mlTotal > 0.0f ? lateral / mlTotal * 100.0f : 50.0f;
  float asymPct    = fabsf(medialPct - lateralPct);

  // ── Heel-vs-forefoot strike ratio
  float hfTotal   = zHeel + zForefoot;
  float heelRatio = hfTotal > 0.0f ? zHeel / hfTotal * 100.0f : 50.0f;

  // ── Loading rate via FSR-jerk extrapolation.
  // FSR 402 saturates at ~10 kg of force, far below running peak GRF
  // (100–200 kg). But the *rate of rise* of the FSR signal during the
  // unsaturated portion of the impact transient encodes impact magnitude.
  //
  // Conversion: gMaxHeelJerk is in ADC-counts/s on the heel composite.
  //   force_at_FSR_saturation = 10 kg × g = 98.1 N
  //   ADC at saturation        = 4095 (12-bit, full scale; assumed)
  //   N per ADC count          = 98.1 / 4095 ≈ 0.02395
  //   body weight (assumed)    = 70 kg → 686.7 N
  //   BW/s per (counts/s)      = 0.02395 / 686.7 ≈ 3.488e-5
  // Healthy runners read 30–80 BW/s; >80 raises stress-fracture risk
  // (Milner 2006). The 70 kg assumption can become user-configurable via
  // /api/settings later — until then the number is "70-kg-equivalent BW/s".
  constexpr float ADC_TO_BWS = (98.1f / 4095.0f) / 686.7f;   // ≈ 3.488e-5
  float loadingRateBWs = gMaxHeelJerk > 0.0f
                       ? gMaxHeelJerk * ADC_TO_BWS
                       : 0.0f;
  (void)gMaxJerkZ;   // IMU vertical jerk still tracked for future fusion

  // ── Pronation: running mean of gyro_x (degrees/s).
  // Net mean ≈ 0 for symmetric gait; positive = pronation, negative = supination.
  // First-order only. Clamp to 0 if below the noise-floor (|x| < 0.5 °/s,
  // well under the 8-15 °/s flag thresholds) so the UI shows "0.0°" instead
  // of an ugly "-0.00°" when the IMU is disconnected or perfectly zeroed.
  float pronate = stats_get_mean(N_FSR + 3);   // channel 9 = gyro_x
  if (fabsf(pronate) < 0.5f) pronate = 0.0f;

  // ── Ground contact time: average of per-step heel-strike→toe-off intervals
  // recorded by the time-domain step detector. 0 until at least one valid
  // contact interval (frontend renders that as "—").
  float contactMs = gContactCount > 0
                  ? (float)gContactSumMs / (float)gContactCount
                  : 0.0f;
  (void)durSec;

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

  if (heelRatio > 65.0f && zHeel > zForefoot + 10.0f) {
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
  j +=   "\"forefoot\":";   j += String(zForefoot, 1);
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
