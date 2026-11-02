#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "Alert.h"
#include "Config.h"
#include "Display.h"
#include "TimeSync.h"

class HttpServer;  // include "HttpServer.h" in the module implementation to use it

// Base class of all device modules. It provides the features shared by every device (WiFi,
// SmsRelay, settings, ...) through the objects it receives; a concrete module derives
// from it and overrides what is specific to the device. The default implementations do nothing,
// so a GenericModule alone is a valid module without any specific feature.
//
// HTML fragment contracts (the fragments are inserted as is into the generic pages):
//
// getStatusHtml(): `<tr><td>Label</td><td id="x">-</td></tr>` rows added to the home page status
//   table, optionally followed by a <script> defining `function moduleStatus(d){...}`. It is
//   called with the /api/status JSON after every refresh (typically to fill the cells).
//
// getConfigHtml(): free HTML (a heading and a <form> for instance) displayed in the module
//   settings page (/module/config, linked from the home and settings pages). It renders
//   the current values itself, and its own script sends the values as JSON with
//   POST /api/module/config, whose body is given to saveConfig(). The error message of a failed
//   save is returned as {"error": "..."} with HTTP status 400. The page only provides the
//   navigation links and /style.css.
class GenericModule {
 public:
  GenericModule(Display& display, Config& config, Alert& alert, HttpServer& httpServer,
                TimeSync& timeSync)
      : display_(display),
        config_(config),
        alert_(alert),
        httpServer_(httpServer),
        timeSync_(timeSync) {}

  virtual ~GenericModule() = default;

  // Identifies the kind of device (e.g. "feeder", "aquamonitor"); concrete modules override it.
  virtual const char* getType() const { return "generic"; }

  // Called once from setup(), before the HTTP server starts (the place to register extra routes).
  virtual void begin() {}

  // Called from loop().
  virtual void update() {}

  // Module-specific part of the /api/status answer. The HTTP server adds the generic fields
  // (name, SmsRelay state, time) to it.
  virtual JsonDocument getStatus() {
    JsonDocument doc;
    doc["message"] = "override the getStatus method";
    return doc;
  }

  // Rows (and optional script) displayed on the home page; see class comment.
  virtual String getStatusHtml() { return String("<h1>Override the getStatusHtml() method</h1>"); }

  // Module-specific settings section; see class comment.
  virtual String getConfigHtml() { return String("<h1>Override the getConfigHtml() method</h1>"); }

  // Receives the JSON posted to /api/module/config. Validates, applies and persists the values
  // (in the module's own NVS namespace). Returns false and sets `error` to reject them.
  virtual bool saveConfig(JsonObjectConst cfg, String& error) {
    (void)cfg;
    error = "This module has no specific settings";
    return false;
  }

 protected:
  Display& display_;
  Config& config_;
  Alert& alert_;
  HttpServer& httpServer_;
  TimeSync& timeSync_;
};
