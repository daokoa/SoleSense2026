#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

const char* ssid = "XIAO-ESP32";
const char* password = "12345678";

AsyncWebServer server(80);

void setup() {
  Serial.begin(115200);
  delay(1000);

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS failed!");
    return;
  }
  Serial.println("LittleFS ready!");

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  ArduinoOTA.setHostname("XIAO-ESP32");
  ArduinoOTA.setPassword("ota123");
  ArduinoOTA.onStart([]() { Serial.println("OTA Starting..."); });
  ArduinoOTA.onEnd([]() { Serial.println("OTA Done!"); });
  ArduinoOTA.onError([](ota_error_t error) { Serial.println("OTA Error!"); });
  ArduinoOTA.begin();
  Serial.println("OTA Ready!");

  // Serve index.html
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/index.html", "text/html");
  });

  // Device info endpoint
  server.on("/api/device", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"firmware\":\"v0.1\",";
    json += "\"board\":\"XIAO ESP32-C3\",";
    json += "\"sampleRateHz\":100,";
    json += "\"state\":\"idle\",";
    json += "\"fs\":{\"usedBytes\":1024,\"totalBytes\":4096000}";
    json += "}";
    request->send(200, "application/json", json);
  });

  // Dummy sensor data endpoint
  server.on("/api/sensor", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"ax\":" + String(random(-100,100)/100.0) + ",";
    json += "\"ay\":" + String(random(-100,100)/100.0) + ",";
    json += "\"az\":" + String(random(-100,100)/100.0) + ",";
    json += "\"gx\":" + String(random(-100,100)/100.0) + ",";
    json += "\"gy\":" + String(random(-100,100)/100.0) + ",";
    json += "\"gz\":" + String(random(-100,100)/100.0) + ",";
    json += "\"fsr\":[";
    for (int i = 0; i < 6; i++) {
      json += String(random(0, 1023));
      if (i < 5) json += ",";
    }
    json += "]}";
    request->send(200, "application/json", json);
  });

  server.begin();
  Serial.println("Web server started!");
}

void loop() {
  ArduinoOTA.handle();
}