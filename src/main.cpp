#include <Arduino.h>
#include <WiFi.h>

#include "Config.h"
#include "Display.h"
#include "HttpServer.h"
#include "Sensor.h"
#include "SmsRelayScanner.h"
#include "TimeSync.h"

constexpr uint32_t STA_TIMEOUT_MS = 30000;
constexpr uint32_t REBOOT_DELAY_MS = 1000;

ConfigStore configStore;
Config config;
Sensor sensor;
TimeSync timeSync;
Display display;
SmsRelayScanner smsRelayScanner;
uint32_t rebootAt = 0;  // 0 = no reboot pending

void scheduleReboot() {
  rebootAt = millis() + REBOOT_DELAY_MS;
  if (rebootAt == 0) rebootAt = 1;
}

HttpServer httpServer(config, configStore, sensor, timeSync, display, smsRelayScanner,
                      scheduleReboot);

// ---------------------------------------------------------------- WiFi

bool connectSta() {
  if (config.homeSsid.isEmpty() || config.homePwd.isEmpty()) {
    return false;
  }

  Serial.printf("Connecting to home network '%s'...\n", config.homeSsid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(config.homeSsid.c_str(), config.homePwd.c_str());

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < STA_TIMEOUT_MS) {
    delay(250);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Connected, IP address: ");
    Serial.println(WiFi.localIP());
    return true;
  }

  Serial.println("Home network connection failed");
  WiFi.disconnect(true);
  return false;
}

void startAp() {
  const bool hasLocal = !config.apSsid.isEmpty() && !config.apPwd.isEmpty();
  const char* ssid = hasLocal ? config.apSsid.c_str() : ConfigDefaults::AP_SSID;
  const char* pwd = hasLocal ? config.apPwd.c_str() : ConfigDefaults::AP_PWD;

  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(ssid, pwd)) {
    Serial.println("Failed to start AP");
    return;
  }
  Serial.printf("AP started (%s credentials), SSID: %s\n", hasLocal ? "stored" : "default", ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.softAPIP());
}

void startWifi() {
  if (!connectSta()) {
    startAp();
  }
}

// ---------------------------------------------------------------- Arduino

void setup() {
  Serial.begin(115200);
  delay(100);

  config = configStore.load();

  display.begin(config.name, config.flipScreen);

  timeSync.begin(config, configStore);
  smsRelayScanner.begin(config, configStore);
  sensor.begin();
  startWifi();

  httpServer.begin();
}

void loop() {
  httpServer.handleClient();
  timeSync.update();
  smsRelayScanner.update();
  display.update(timeSync, smsRelayScanner.isNotFound());
  display.refresh();

  if (rebootAt != 0 && static_cast<int32_t>(millis() - rebootAt) >= 0) {
    Serial.println("Rebooting");
    ESP.restart();
  }
}
