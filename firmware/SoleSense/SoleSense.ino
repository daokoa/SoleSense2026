// =============================================================================
// SoleSense v0.1 — XIAO ESP32-C3 firmware
// =============================================================================

#include <WiFi.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>

static const char* AP_SSID = "SoleSense";
static const char* AP_PASS = "solesense";

AsyncWebServer server(80);

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

  WiFi.softAP(AP_SSID, AP_PASS);
  IPAddress ip = WiFi.softAPIP();
  Serial.printf("[WiFi] AP '%s' up at %s\n", AP_SSID, ip.toString().c_str());

  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  server.begin();
  Serial.println("[HTTP] server started");
}

void loop() {
  delay(1);
}
