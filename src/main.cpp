#include <Arduino.h>
#include "Platform.h"

#include "Alert.h"
#include "AquaMonitor.h"
#include "Config.h"
#include "Display.h"
#include "GenericModule.h"
#include "HttpServer.h"
#include "SmsRelayScanner.h"
#include "TimeSync.h"

constexpr uint32_t STA_TIMEOUT_MS = 30000;
constexpr uint32_t REBOOT_DELAY_MS = 1000;
constexpr uint32_t QR_DURATION_MS = 20000;
constexpr uint32_t FALLBACK_CHECK_MS = 10000;      // check WiFi status every 10 seconds
constexpr uint32_t FALLBACK_PING_INTERVAL_MS = 30000;  // ping fallback AP every 30 seconds

ConfigStore configStore;
Config config;
TimeSync timeSync;
Display display;
Alert alert;
SmsRelayScanner smsRelayScanner;
uint32_t rebootAt = 0;  // 0 = no reboot pending
bool usingFallbackWifi = false;  // true when connected to fallback AP
uint32_t lastWifiCheck = 0;  // last time we checked WiFi status
uint32_t lastFallbackPing = 0;  // last time we pinged the fallback AP
bool fallbackApAvailable = false;  // false when fallback AP is unreachable

void scheduleReboot() {
  rebootAt = millis() + REBOOT_DELAY_MS;
  if (rebootAt == 0) rebootAt = 1;
}

HttpServer httpServer(config, configStore, timeSync, display, smsRelayScanner, scheduleReboot);

// Replace GenericModule by the class of the actual device module.
AquaMonitor module(display, config, alert, httpServer, timeSync);

// ---------------------------------------------------------------- WiFi

// Forward declarations
void startAp();

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
    disableWifiSleep();
    Serial.print("Connected, IP address: ");
    Serial.println(WiFi.localIP());
    usingFallbackWifi = false;
    smsRelayScanner.onWifiConnected(false);
    return true;
  }

  Serial.println("Home network connection failed");
  WiFi.disconnect(false);
  return false;
}

bool connectFallbackAp() {
  if (config.fallbackSsid.isEmpty()) {
    Serial.println("Fallback WiFi credentials have not been received from SmsRelay yet");
    return false;
  }

  Serial.printf("Connecting to fallback network '%s'...\n", config.fallbackSsid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false);
  WiFi.begin(config.fallbackSsid.c_str(), config.fallbackPwd.c_str());

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < STA_TIMEOUT_MS) {
    delay(250);
  }

  if (WiFi.status() == WL_CONNECTED) {
    disableWifiSleep();
    Serial.print("Connected to fallback network, IP address: ");
    Serial.println(WiFi.localIP());
    usingFallbackWifi = true;
    smsRelayScanner.onWifiConnected(true);
    lastFallbackPing = millis();
    return true;
  }

  Serial.println("Fallback network connection failed");
  WiFi.disconnect(false);
  usingFallbackWifi = false;
  return false;
}

// Monitor WiFi connectivity and fallback to SmsRelay AP if home WiFi is lost
void updateWifiStatus() {
  const uint32_t now = millis();
  if (now - lastWifiCheck < FALLBACK_CHECK_MS) {
    return;
  }
  lastWifiCheck = now;

  const bool homeWifiConnected = (WiFi.getMode() == WIFI_STA && WiFi.status() == WL_CONNECTED);

  // If we're on home WiFi and it's still available, keep it
  if (homeWifiConnected && !usingFallbackWifi) {
    return;
  }

  // Stay on fallback while its WiFi connection is available, even if a ping fails.
  if (usingFallbackWifi) {
    if (homeWifiConnected) {
      if (now - lastFallbackPing >= FALLBACK_PING_INTERVAL_MS) {
        fallbackApAvailable = smsRelayScanner.verify("192.168.4.1");
        lastFallbackPing = millis();
      }
      return;
    }
    usingFallbackWifi = false;
    fallbackApAvailable = false;
    Serial.println("Fallback WiFi disconnected, trying home WiFi");
    if (!connectSta()) connectFallbackAp();
    lastWifiCheck = millis();
    return;
  }

  // Home WiFi is disconnected and we're not on fallback yet
  if (!homeWifiConnected) {
    if (WiFi.getMode() == WIFI_AP) return;  // initial setup, no STA credentials configured
    Serial.println("Home WiFi disconnected, attempting to use fallback WiFi");
    if (!connectFallbackAp()) {
      Serial.println("Fallback WiFi unavailable; staying in STA mode and retrying home WiFi");
      connectSta();
    }
    lastWifiCheck = millis();
  }
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
  if (config.homeSsid.isEmpty() && config.fallbackSsid.isEmpty()) {
    startAp();
    return;
  }
  WiFi.mode(WIFI_STA);
  if (!connectSta() && !connectFallbackAp()) {
    Serial.println("Home and fallback WiFi unavailable; staying in STA mode to retry");
  }
  lastWifiCheck = millis();
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
  updateWifiStatus();
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
