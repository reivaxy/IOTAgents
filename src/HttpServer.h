#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <WebServer.h>

#include "BuildInfo.h"
#include "Config.h"
#include "Display.h"
#include "GenericModule.h"
#include "SmsRelayScanner.h"
#include "TimeSync.h"
#include "WebPages.h"

// HTTP server: web pages, JSON API (status, config, display) and OTA firmware update.
// Call begin() once from setup(), then handleClient() from loop().
class HttpServer {
 public:
  using RebootScheduler = void (*)();

  HttpServer(Config& config, ConfigStore& configStore, TimeSync& timeSync, Display& display,
             SmsRelayScanner& smsRelayScanner, RebootScheduler scheduleReboot)
      : config_(config),
        configStore_(configStore),
        timeSync_(timeSync),
        display_(display),
        smsRelayScanner_(smsRelayScanner),
        scheduleReboot_(scheduleReboot) {}

  // The module and the server reference each other, hence this setter (call before begin()).
  void setModule(GenericModule& module) { module_ = &module; }

  // For the modules, to register their own routes and answer on them.
  WebServer& webServer() { return server_; }

  void sendJson(int code, const JsonDocument& doc, bool pretty = false) {
    String out;
    if (pretty) {
      serializeJsonPretty(doc, out);
    } else {
      serializeJson(doc, out);
    }
    server_.send(code, "application/json", out);
  }

  void sendError(int code, const char* message) {
    JsonDocument doc;
    doc["error"] = message;
    sendJson(code, doc);
  }

  void begin() {
    setupRoutes();
    server_.begin();
    Serial.println("Web server started on port 80");
  }

  void handleClient() { server_.handleClient(); }

 private:
  static constexpr size_t NAME_MAX_LEN = 10;
  static constexpr size_t SSID_MAX_LEN = 32;
  static constexpr size_t PWD_MIN_LEN = 8;
  static constexpr size_t PWD_MAX_LEN = 63;
  static constexpr const char* MODULE_STATUS_MARKER = "<!--MODULE_STATUS-->";
  static constexpr const char* MODULE_CONFIG_MARKER = "<!--MODULE_CONFIG-->";

  WebServer server_{80};
  Config& config_;
  ConfigStore& configStore_;
  TimeSync& timeSync_;
  Display& display_;
  SmsRelayScanner& smsRelayScanner_;
  RebootScheduler scheduleReboot_;
  GenericModule* module_ = nullptr;

  String updateError_;
  bool updateDone_ = false;

  void setupRoutes() {
    server_.on("/", HTTP_GET, [this]() {
      sendPage(HOME_PAGE, MODULE_STATUS_MARKER, module_ ? module_->getStatusHtml() : String());
    });
    server_.on("/config", HTTP_GET, [this]() { server_.send(200, "text/html", CONFIG_PAGE); });
    server_.on("/module/config", HTTP_GET, [this]() {
      sendPage(MODULE_CONFIG_PAGE, MODULE_CONFIG_MARKER,
               module_ ? module_->getConfigHtml() : String());
    });
    server_.on("/style.css", HTTP_GET, [this]() { server_.send(200, "text/css", STYLE); });
    server_.on("/api/status", HTTP_GET, [this]() { handleStatus(); });
    server_.on("/api/ping", HTTP_GET, [this]() { server_.send(200, "text/plain", config_.name); });
    server_.on("/api/config", HTTP_GET, [this]() { handleGetConfig(); });
    server_.on("/api/config", HTTP_POST, [this]() { handleSetConfig(); });
    server_.on("/api/module/config", HTTP_POST, [this]() { handleSetModuleConfig(); });
    server_.on("/api/display", HTTP_POST, [this]() { handleSetDisplay(); });
    server_.on("/api/smsrelay/scan", HTTP_POST, [this]() { handleSmsRelayScan(); });
    server_.on("/api/smsrelay/ping", HTTP_POST, [this]() { handleSmsRelayPing(); });
    server_.on(
        "/api/update", HTTP_POST, [this]() { handleUpdateDone(); },
        [this]() { handleUpdateUpload(); });
    server_.onNotFound([this]() { server_.send(404, "text/plain", "Not found"); });
  }

  // Sends a page streamed in chunks, with `fragment` inserted in place of `marker`.
  void sendPage(const char* page, const char* marker, const String& fragment) {
    const char* at = strstr(page, marker);
    const size_t before = at ? static_cast<size_t>(at - page) : strlen(page);
    server_.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server_.send(200, "text/html", "");
    // An empty chunk ends the transfer: never send one before the end.
    if (before > 0) server_.sendContent(page, before);
    if (at) {
      if (!fragment.isEmpty()) server_.sendContent(fragment);
      server_.sendContent(String(at + strlen(marker)));
    }
    server_.sendContent("");
  }

  void handleStatus() {
    JsonDocument doc;
    if (module_ != nullptr) {
      JsonDocument moduleStatus = module_->getStatus();
      doc["module"].set(moduleStatus.as<JsonVariantConst>());
    }
    doc["name"] = config_.name;
    doc["type"] = module_ ? module_->getType() : "generic";
    doc["smsRelayNotFound"] = smsRelayScanner_.isNotFound();
    doc["smsRelayIp"] = config_.smsRelayIp;
    if (timeSync_.isSynced()) {
      doc["epoch"] = static_cast<int64_t>(timeSync_.now());
      doc["utcOffset"] = timeSync_.utcOffset();
    } else {
      doc["epoch"] = nullptr;
    }
    sendJson(200, doc, false);
  }

  void handleGetConfig() {
    // Passwords are intentionally never exposed.
    JsonDocument doc;
    doc["name"] = config_.name;
    doc["homeSsid"] = config_.homeSsid;
    doc["apSsid"] = config_.apSsid;
    doc["apSsidDefault"] = ConfigDefaults::AP_SSID;
    doc["flipScreen"] = config_.flipScreen;
    doc["smsRelayIp"] = config_.smsRelayIp;
    doc["smsRelayScanning"] = smsRelayScanner_.isScanning();
    doc["gitHash"] = BUILD_GIT_HASH;
    doc["buildTime"] = BUILD_TIME;
    sendJson(200, doc);
  }

  // Reads an optional string field. Returns false if present with a non-string type.
  static bool readString(const JsonDocument& doc, const char* key, String& out, bool& present) {
    JsonVariantConst v = doc[key];
    present = !v.isNull();
    if (!present) return true;
    if (!v.is<const char*>()) return false;
    out = v.as<const char*>();
    return true;
  }

  static bool readBool(const JsonDocument& doc, const char* key, bool& out, bool& present) {
    JsonVariantConst v = doc[key];
    present = !v.isNull();
    if (!present) return true;
    if (!v.is<bool>()) return false;
    out = v.as<bool>();
    return true;
  }

  void handleSetConfig() {
    JsonDocument doc;
    if (deserializeJson(doc, server_.arg("plain"))) {
      sendError(400, "Invalid JSON");
      return;
    }

    Config next = config_;
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
    if (config_.apSsid.isEmpty() && !apPwdGiven && next.apSsid == ConfigDefaults::AP_SSID) {
      next.apSsid = "";
    }

    if (next.name.isEmpty() || next.name.length() > NAME_MAX_LEN) {
      sendError(400, "Module name must be 1-10 characters");
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

    const bool wifiChanged = next.homeSsid != config_.homeSsid || next.homePwd != config_.homePwd ||
                             next.apSsid != config_.apSsid || next.apPwd != config_.apPwd;

    config_ = next;
    configStore_.save(config_);
    display_.setTitle(config_.name);

    JsonDocument res;
    res["ok"] = true;
    res["reboot"] = wifiChanged;
    sendJson(200, res);

    if (wifiChanged) {
      scheduleReboot_();
    }
  }

  // Hands the posted JSON object to the module, which validates and persists it.
  void handleSetModuleConfig() {
    JsonDocument doc;
    if (deserializeJson(doc, server_.arg("plain")) || !doc.is<JsonObject>()) {
      sendError(400, "Invalid JSON");
      return;
    }
    String error;
    if (module_ == nullptr || !module_->saveConfig(doc.as<JsonObjectConst>(), error)) {
      sendError(400, error.isEmpty() ? "Module settings rejected" : error.c_str());
      return;
    }
    JsonDocument res;
    res["ok"] = true;
    sendJson(200, res);
  }

  // Saves the screen orientation and applies it immediately (no reboot needed).
  void handleSetDisplay() {
    JsonDocument doc;
    if (deserializeJson(doc, server_.arg("plain"))) {
      sendError(400, "Invalid JSON");
      return;
    }
    JsonVariantConst flip = doc["flipScreen"];
    if (!flip.is<bool>()) {
      sendError(400, "flipScreen must be a boolean");
      return;
    }

    config_.flipScreen = flip.as<bool>();
    configStore_.saveFlipScreen(config_);
    display_.setFlip(config_.flipScreen);

    JsonDocument res;
    res["ok"] = true;
    sendJson(200, res);
  }

  // Forces a new SmsRelay scan; it runs in the background from loop() and updates the stored IP.
  void handleSmsRelayScan() {
    if (!smsRelayScanner_.start()) {
      sendError(409, "Not connected to the home network");
      return;
    }
    JsonDocument res;
    res["ok"] = true;
    res["scanning"] = true;
    sendJson(202, res);
  }

  // Pings the provided SmsRelay IP (blocks for a few seconds) and reports whether it answered.
  // If successful, saves the IP to preferences.
  void handleSmsRelayPing() {
    JsonDocument doc;
    if (deserializeJson(doc, server_.arg("plain"))) {
      sendError(400, "Invalid JSON");
      return;
    }

    JsonVariantConst ipVar = doc["smsRelayIp"];
    if (!ipVar.is<const char*>()) {
      sendError(400, "smsRelayIp must be a string");
      return;
    }

    String smsRelayIp = ipVar.as<const char*>();
    smsRelayIp.trim();

    if (smsRelayIp.isEmpty()) {
      sendError(400, "SmsRelay IP cannot be empty");
      return;
    }

    IPAddress relayIpAddr;
    if (!relayIpAddr.fromString(smsRelayIp)) {
      sendError(400, "SmsRelay IP is not a valid IPv4 address");
      return;
    }

    if (!SmsRelayScanner::isStaConnected()) {
      sendError(409, "Not connected to the home network");
      return;
    }

    bool reachable = smsRelayScanner_.verify(smsRelayIp, SmsRelayScanner::SAVE_CHECK_TIMEOUT_MS);
    
    // If ping successful, save the IP
    if (reachable) {
      config_.smsRelayIp = smsRelayIp;
      configStore_.saveSmsRelayIp(config_);
    }

    JsonDocument res;
    res["ok"] = true;
    res["reachable"] = reachable;
    sendJson(200, res);
  }

  // Receives the multipart firmware upload chunk by chunk and writes it to the next OTA partition.
  void handleUpdateUpload() {
    HTTPUpload& upload = server_.upload();
    switch (upload.status) {
      case UPLOAD_FILE_START:
        updateError_ = "";
        updateDone_ = false;
        Serial.printf("OTA update started: %s\n", upload.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
          updateError_ = Update.errorString();
        }
        break;
      case UPLOAD_FILE_WRITE:
        if (updateError_.isEmpty() &&
            Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
          updateError_ = Update.errorString();
        }
        break;
      case UPLOAD_FILE_END:
        if (updateError_.isEmpty()) {
          if (Update.end(true)) {
            updateDone_ = true;
          } else {
            updateError_ = Update.errorString();
          }
        }
        break;
      case UPLOAD_FILE_ABORTED:
        Update.abort();
        updateError_ = "Upload aborted";
        break;
    }
  }

  void handleUpdateDone() {
    if (updateDone_ && updateError_.isEmpty()) {
      Serial.println("OTA update succeeded");
      JsonDocument res;
      res["ok"] = true;
      res["reboot"] = true;
      sendJson(200, res);
      scheduleReboot_();
    } else {
      Serial.printf("OTA update failed: %s\n", updateError_.c_str());
      sendError(500, updateError_.isEmpty() ? "No firmware received" : updateError_.c_str());
    }
    updateDone_ = false;
  }
};
