#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "Platform.h"

struct Config;
class ConfigStore;
class Display;
class GenericModule;
class SmsRelayScanner;
class TimeSync;

// HTTP server: web pages, JSON API (status, config, display) and OTA firmware update.
// Call begin() once from setup(), then handleClient() from loop().
class HttpServer {
 public:
  using RebootScheduler = void (*)();

  HttpServer(Config& config, ConfigStore& configStore, TimeSync& timeSync, Display& display,
             SmsRelayScanner& smsRelayScanner, RebootScheduler scheduleReboot);

  // The module and the server reference each other, hence this setter (call before begin()).
  void setModule(GenericModule& module);

  // For the modules, to register their own routes and answer on them.
  WebServer& webServer();

  void sendJson(int code, const JsonDocument& doc, bool pretty = false);
  void sendError(int code, const char* message);
  void begin();
  void handleClient();

  // Shared by the HTTP status endpoint and outgoing SmsRelay pings.
  JsonDocument getStatus();

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

  void setupRoutes();

  // Sends a page streamed in chunks, with `fragment` inserted in place of `marker`.
  void sendPage(const char* page, const char* marker, const String& fragment);
  void handleStatus();
  void handleGetConfig();

  // Reads an optional string field. Returns false if present with a non-string type.
  static bool readString(const JsonDocument& doc, const char* key, String& out, bool& present);
  static bool readBool(const JsonDocument& doc, const char* key, bool& out, bool& present);
  void handleSetConfig();

  // Hands the posted JSON object to the module, which validates and persists it.
  void handleSetModuleConfig();

  // Saves the screen orientation and applies it immediately (no reboot needed).
  void handleSetDisplay();
  void handleRestart();

  // Forces a new SmsRelay scan; it runs in the background from loop() and updates the stored IP.
  void handleSmsRelayScan();

  // Pings the provided SmsRelay IP (blocks for a few seconds) and reports whether it answered.
  // If successful, saves the IP to preferences.
  void handleSmsRelayPing();

  // Receives the multipart firmware upload chunk by chunk and writes it to the next OTA partition.
  void handleUpdateUpload();
  void handleUpdateDone();
};
