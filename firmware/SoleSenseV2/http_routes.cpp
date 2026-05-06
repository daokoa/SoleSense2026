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

#include <LittleFS.h>
#include <WiFi.h>

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
static void handle_run_report(AsyncWebServerRequest* req) {
  // TODO Task 6: compute final metrics from the FFT bins + outlier buffer.
  //   - cadence  = peak bin in heel-FSR FFT × 60
  //   - gct      = derived from outlier-spike spacing in the heel channel
  //   - pronate  = gyro-x low-frequency content
  //   - balance  = energy ratio between medial and lateral FSR channels
  //   - flags[]  = injury-flag list with thresholds from the spec
  //
  // Return shape (placeholder for now):
  String j = "{";
  j += "\"cadence\":0,";
  j += "\"contactMs\":0,";
  j += "\"loadingRate\":0,";
  j += "\"pronation\":0,";
  j += "\"balanceLeft\":50,\"balanceRight\":50,";
  j += "\"zoneDist\":{\"heel\":25,\"midfoot\":25,\"ball\":25,\"toe\":25},";
  j += "\"flags\":[],";
  j += "\"durationMs\":"; j += (unsigned long)gRunElapsedMs; j += ",";
  j += "\"samples\":";    j += (unsigned long)gSampleCount;  j += ",";
  j += "\"outliers\":";   j += (unsigned)outliers_count();
  j += ",\"_note\":\"placeholder — analysis lives in v0.2 firmware Task 6\"";
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
  server.on("/api/fft-selftest",   HTTP_POST, handle_fft_selftest);
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
}
