#include <Arduino.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>

#include "BuildInfo.h"
#include "Config.h"
#include "Display.h"
#include "Sensor.h"
#include "TimeSync.h"
#include "WebPages.h"

constexpr uint32_t STA_TIMEOUT_MS = 30000;
constexpr uint32_t REBOOT_DELAY_MS = 1000;
constexpr size_t NAME_MAX_LEN = 32;
constexpr size_t SSID_MAX_LEN = 32;
constexpr size_t PWD_MIN_LEN = 8;
constexpr size_t PWD_MAX_LEN = 63;

WebServer server(80);
ConfigStore configStore;
Config config;
Sensor sensor;
TimeSync timeSync;
Display display;
uint32_t rebootAt = 0;  // 0 = no reboot pending

void scheduleReboot() {
  rebootAt = millis() + REBOOT_DELAY_MS;
  if (rebootAt == 0) rebootAt = 1;
}

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

// ---------------------------------------------------------------- HTTP

void sendJson(int code, const JsonDocument& doc) {
  String out;
  serializeJson(doc, out);
  server.send(code, "application/json", out);
}

void sendError(int code, const char* message) {
  JsonDocument doc;
  doc["error"] = message;
  sendJson(code, doc);
}

void handleStatus() {
  JsonDocument doc;
  doc["name"] = config.name;
  doc["light"] = sensor.isLightOn();
  doc["temperature"] = sensor.readTemperature();
  doc["waterLevelOk"] = sensor.isWaterLevelOk();
  if (timeSync.isSynced()) {
    doc["epoch"] = static_cast<int64_t>(timeSync.now());
    doc["utcOffset"] = timeSync.utcOffset();
  } else {
    doc["epoch"] = nullptr;
  }
  sendJson(200, doc);
}

void handleGetConfig() {
  // Passwords are intentionally never exposed.
  JsonDocument doc;
  doc["name"] = config.name;
  doc["homeSsid"] = config.homeSsid;
  doc["apSsid"] = config.apSsid;
  doc["apSsidDefault"] = ConfigDefaults::AP_SSID;
  doc["flipScreen"] = config.flipScreen;
  doc["gitHash"] = BUILD_GIT_HASH;
  doc["buildTime"] = BUILD_TIME;
  sendJson(200, doc);
}

// Reads an optional string field. Returns false if present with a non-string type.
bool readString(const JsonDocument& doc, const char* key, String& out, bool& present) {
  JsonVariantConst v = doc[key];
  present = !v.isNull();
  if (!present) return true;
  if (!v.is<const char*>()) return false;
  out = v.as<const char*>();
  return true;
}

void handleSetConfig() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    sendError(400, "Invalid JSON");
    return;
  }

  Config next = config;
  bool present;
  bool typesOk = true;
  bool homePwdGiven = false;
  bool apPwdGiven = false;

  typesOk &= readString(doc, "name", next.name, present);
  typesOk &= readString(doc, "homeSsid", next.homeSsid, present);
  typesOk &= readString(doc, "apSsid", next.apSsid, present);
  typesOk &= readString(doc, "homePwd", next.homePwd, homePwdGiven);
  typesOk &= readString(doc, "apPwd", next.apPwd, apPwdGiven);
  if (!typesOk) {
    sendError(400, "All fields must be strings");
    return;
  }

  next.name.trim();
  next.homeSsid.trim();
  next.apSsid.trim();

  // The page prefills the default AP SSID when none is stored; submitting it unchanged is a no-op.
  if (config.apSsid.isEmpty() && !apPwdGiven && next.apSsid == ConfigDefaults::AP_SSID) {
    next.apSsid = "";
  }

  if (next.name.isEmpty() || next.name.length() > NAME_MAX_LEN) {
    sendError(400, "Module name must be 1-32 characters");
    return;
  }
  if (next.homeSsid.length() > SSID_MAX_LEN || next.apSsid.length() > SSID_MAX_LEN) {
    sendError(400, "SSID must be at most 32 characters");
    return;
  }
  if (next.homePwd.length() > PWD_MAX_LEN) {
    sendError(400, "Home password must be at most 63 characters");
    return;
  }
  if (apPwdGiven && (next.apPwd.length() < PWD_MIN_LEN || next.apPwd.length() > PWD_MAX_LEN)) {
    sendError(400, "Local password must be 8-63 characters");
    return;
  }
  if (!next.apSsid.isEmpty() && next.apPwd.isEmpty()) {
    sendError(400, "A local password is required with a local SSID");
    return;
  }

  // Clearing an SSID also clears its stored password.
  if (next.homeSsid.isEmpty()) next.homePwd = "";
  if (next.apSsid.isEmpty()) next.apPwd = "";

  const bool wifiChanged = next.homeSsid != config.homeSsid || next.homePwd != config.homePwd ||
                           next.apSsid != config.apSsid || next.apPwd != config.apPwd;

  config = next;
  configStore.save(config);
  display.setTitle(config.name);

  JsonDocument res;
  res["ok"] = true;
  res["reboot"] = wifiChanged;
  sendJson(200, res);

  if (wifiChanged) {
    scheduleReboot();
  }
}

// Saves the screen orientation and applies it immediately (no reboot needed).
void handleSetDisplay() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    sendError(400, "Invalid JSON");
    return;
  }
  JsonVariantConst flip = doc["flipScreen"];
  if (!flip.is<bool>()) {
    sendError(400, "flipScreen must be a boolean");
    return;
  }

  config.flipScreen = flip.as<bool>();
  configStore.saveFlipScreen(config);
  display.setFlip(config.flipScreen);

  JsonDocument res;
  res["ok"] = true;
  sendJson(200, res);
}

// ---------------------------------------------------------------- OTA

String updateError;
bool updateDone = false;

// Receives the multipart firmware upload chunk by chunk and writes it to the next OTA partition.
void handleUpdateUpload() {
  HTTPUpload& upload = server.upload();
  switch (upload.status) {
    case UPLOAD_FILE_START:
      updateError = "";
      updateDone = false;
      Serial.printf("OTA update started: %s\n", upload.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        updateError = Update.errorString();
      }
      break;
    case UPLOAD_FILE_WRITE:
      if (updateError.isEmpty() && Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        updateError = Update.errorString();
      }
      break;
    case UPLOAD_FILE_END:
      if (updateError.isEmpty()) {
        if (Update.end(true)) {
          updateDone = true;
        } else {
          updateError = Update.errorString();
        }
      }
      break;
    case UPLOAD_FILE_ABORTED:
      Update.abort();
      updateError = "Upload aborted";
      break;
  }
}

void handleUpdateDone() {
  if (updateDone && updateError.isEmpty()) {
    Serial.println("OTA update succeeded");
    JsonDocument res;
    res["ok"] = true;
    res["reboot"] = true;
    sendJson(200, res);
    scheduleReboot();
  } else {
    Serial.printf("OTA update failed: %s\n", updateError.c_str());
    sendError(500, updateError.isEmpty() ? "No firmware received" : updateError.c_str());
  }
  updateDone = false;
}

void setupRoutes() {
  server.on("/", HTTP_GET, []() { server.send(200, "text/html", HOME_PAGE); });
  server.on("/config", HTTP_GET, []() { server.send(200, "text/html", CONFIG_PAGE); });
  server.on("/style.css", HTTP_GET, []() { server.send(200, "text/css", STYLE); });
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/config", HTTP_POST, handleSetConfig);
  server.on("/api/display", HTTP_POST, handleSetDisplay);
  server.on("/api/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
}

// ---------------------------------------------------------------- Arduino

void setup() {
  Serial.begin(115200);
  delay(100);

  config = configStore.load();

  display.begin(config.name, config.flipScreen);

  timeSync.begin(config, configStore);
  sensor.begin();
  startWifi();

  setupRoutes();
  server.begin();
  Serial.println("Web server started on port 80");
}

void loop() {
  server.handleClient();
  timeSync.update();
  display.update(timeSync);
  display.refresh();

  if (rebootAt != 0 && static_cast<int32_t>(millis() - rebootAt) >= 0) {
    Serial.println("Rebooting");
    ESP.restart();
  }
}
