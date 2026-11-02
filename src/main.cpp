#include <Arduino.h>
#include <WiFi.h>

#include "Alert.h"
#include "Config.h"
#include "Display.h"
#include "GenericModule.h"
#include "HttpServer.h"
#include "SmsRelayScanner.h"
#include "TimeSync.h"

constexpr uint32_t STA_TIMEOUT_MS = 30000;
constexpr uint32_t REBOOT_DELAY_MS = 1000;
constexpr uint32_t QR_DURATION_MS = 20000;

ConfigStore configStore;
Config config;
TimeSync timeSync;
Display display;
Alert alert;
SmsRelayScanner smsRelayScanner;
uint32_t rebootAt = 0;  // 0 = no reboot pending

void scheduleReboot() {
  rebootAt = millis() + REBOOT_DELAY_MS;
  if (rebootAt == 0) rebootAt = 1;
}

HttpServer httpServer(config, configStore, timeSync, display, smsRelayScanner, scheduleReboot);

// Replace GenericModule by the class of the actual device module.
GenericModule module(display, config, alert, httpServer, timeSync);

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
    // Modem sleep delays ARP replies and SYN-ACKs (buffered by the AP until the next beacon),
    // which makes short connect timeouts fail even for hosts that are up (SmsRelay checks and
    // alerts). The module is mains powered, so keep the radio awake.
    WiFi.setSleep(false);
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
  alert.begin(config);
  smsRelayScanner.begin(config, configStore, alert, module);
  module.begin();
  startWifi();

  httpServer.setModule(module);
  httpServer.begin();

  const IPAddress ip = WiFi.getMode() == WIFI_AP ? WiFi.softAPIP() : WiFi.localIP();
  display.showQr(String("http://") + ip.toString() + "/", QR_DURATION_MS);
}

void loop() {
  httpServer.handleClient();
  timeSync.update();
  smsRelayScanner.update();
  module.update();
  display.update(timeSync, smsRelayScanner.isNotFound());
  display.refresh();

  if (rebootAt != 0 && static_cast<int32_t>(millis() - rebootAt) >= 0) {
    Serial.println("Rebooting");
    ESP.restart();
  }
}
