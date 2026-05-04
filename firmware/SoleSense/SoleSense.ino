// =============================================================================
// SoleSense v0.1 — XIAO ESP32-C3 firmware
// =============================================================================

#include <WiFi.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>

static const char* AP_SSID = "SoleSense";
static const char* AP_PASS = "solesense";

AsyncWebServer server(80);

// =============================================================================
// HTTP routes
// =============================================================================

enum State { IDLE, RECORDING };
volatile State gState = IDLE;

struct Thresholds {
  int hlr        = 100;
  int proneMax   = 15;
  int proneMin   = -8;
  int gct        = 300;
  int cadenceMin = 160;
};
Thresholds gThresholds;

// =============================================================================
// NVS settings (Preferences)
// =============================================================================

Preferences gPrefs;

static void loadSettings() {
  gPrefs.begin("solesense", false);
  gThresholds.hlr        = gPrefs.getInt("hlr",        100);
  gThresholds.proneMax   = gPrefs.getInt("proneMax",   15);
  gThresholds.proneMin   = gPrefs.getInt("proneMin",   -8);
  gThresholds.gct        = gPrefs.getInt("gct",        300);
  gThresholds.cadenceMin = gPrefs.getInt("cadenceMin", 160);
  Serial.printf("[NVS] thresholds loaded: hlr=%d proneMax=%d proneMin=%d gct=%d cadMin=%d\n",
    gThresholds.hlr, gThresholds.proneMax, gThresholds.proneMin,
    gThresholds.gct, gThresholds.cadenceMin);
}

static void saveThresholds() {
  gPrefs.putInt("hlr",        gThresholds.hlr);
  gPrefs.putInt("proneMax",   gThresholds.proneMax);
  gPrefs.putInt("proneMin",   gThresholds.proneMin);
  gPrefs.putInt("gct",        gThresholds.gct);
  gPrefs.putInt("cadenceMin", gThresholds.cadenceMin);
}

static String stateName(State s) { return s == IDLE ? "idle" : "recording"; }

static void handleDevice(AsyncWebServerRequest* req) {
  String json = "{";
  json += "\"firmware\":\"SoleSense v0.1\",";
  json += "\"board\":\"XIAO ESP32-C3\",";
  json += "\"sampleRateHz\":50,";
  json += "\"state\":\"" + stateName(gState) + "\",";
  json += "\"fs\":{\"totalBytes\":" + String((unsigned)LittleFS.totalBytes());
  json +=        ",\"usedBytes\":"  + String((unsigned)LittleFS.usedBytes()) + "},";
  json += "\"hasData\":" + String(LittleFS.exists("/data.csv") ? "true" : "false") + ",";
  json += "\"thresholds\":{";
  json +=   "\"hlr\":"        + String(gThresholds.hlr) + ",";
  json +=   "\"proneMax\":"   + String(gThresholds.proneMax) + ",";
  json +=   "\"proneMin\":"   + String(gThresholds.proneMin) + ",";
  json +=   "\"gct\":"        + String(gThresholds.gct) + ",";
  json +=   "\"cadenceMin\":" + String(gThresholds.cadenceMin);
  json += "}}";
  req->send(200, "application/json", json);
}

struct Range { int lo, hi; };
static const Range R_HLR        = {1, 10000};
static const Range R_PRONE_MAX  = {0, 90};
static const Range R_PRONE_MIN  = {-90, 0};
static const Range R_GCT        = {50, 2000};
static const Range R_CAD        = {60, 300};

static bool readIntParam(AsyncWebServerRequest* req, const char* key, Range r,
                         int& out, String& err) {
  if (!req->hasParam(key, true)) return true;       // optional, leave unchanged
  int v = req->getParam(key, true)->value().toInt();
  if (v < r.lo || v > r.hi) {
    err = String(key) + " out of range";
    return false;
  }
  out = v;
  return true;
}

static void handleSettings(AsyncWebServerRequest* req) {
  Thresholds next = gThresholds;
  String err;
  if (!readIntParam(req, "hlr",        R_HLR,       next.hlr,        err) ||
      !readIntParam(req, "proneMax",   R_PRONE_MAX, next.proneMax,   err) ||
      !readIntParam(req, "proneMin",   R_PRONE_MIN, next.proneMin,   err) ||
      !readIntParam(req, "gct",        R_GCT,       next.gct,        err) ||
      !readIntParam(req, "cadenceMin", R_CAD,       next.cadenceMin, err)) {
    req->send(400, "application/json",
              "{\"ok\":false,\"error\":\"" + err + "\"}");
    return;
  }
  gThresholds = next;
  saveThresholds();
  req->send(200, "application/json", "{\"ok\":true}");
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("=== SoleSense booting ===");

  if (!LittleFS.begin()) {
    Serial.println("[FS] mount FAILED");
  } else {
    Serial.printf("[FS] Mounted - %u / %u bytes used\n",
                  (unsigned)LittleFS.usedBytes(), (unsigned)LittleFS.totalBytes());
  }

  loadSettings();

  WiFi.softAP(AP_SSID, AP_PASS);
  IPAddress ip = WiFi.softAPIP();
  Serial.printf("[WiFi] AP '%s' up at %s\n", AP_SSID, ip.toString().c_str());

  server.on("/api/device",   HTTP_GET,  handleDevice);
  server.on("/api/settings", HTTP_POST, handleSettings);
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  server.begin();
  Serial.println("[HTTP] server started");
}

void loop() {
  delay(1);
}
