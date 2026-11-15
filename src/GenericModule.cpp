#include "GenericModule.h"

GenericModule::GenericModule(Display& display, Config& config, Alert& alert,
                             HttpServer& httpServer, TimeSync& timeSync)
    : display_(display),
      config_(config),
      alert_(alert),
      httpServer_(httpServer),
      timeSync_(timeSync) {}

GenericModule::~GenericModule() = default;

const char* GenericModule::getType() const { return "generic"; }

void GenericModule::begin() {}

void GenericModule::update() {}

JsonDocument GenericModule::getStatus() {
  JsonDocument doc;
  doc["message"] = "override the getStatus method";
  return doc;
}

String GenericModule::getHomeHtml() {
  return String("<h1>Override the getHomeHtml() method</h1>");
}

String GenericModule::getConfigHtml() {
  return String("<h1>Override the getConfigHtml() method</h1>");
}

bool GenericModule::saveConfig(JsonObjectConst cfg, String& error) {
  (void)cfg;
  error = "This module has no specific settings";
  return false;
}