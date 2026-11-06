#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include <Preferences.h>

#include "GenericModule.h"
#include "HttpServer.h"

// AquaMonitor module configuration and status
struct AquaMonitorConfig {
  // Light schedule 1 (on)
  int8_t light1OnStart = 8;    // hour (0-23)
  int8_t light1OnEnd = 10;     // hour (0-23)
  // Light schedule 1 (off)
  int8_t light1OffStart = 18;  // hour (0-23)
  int8_t light1OffEnd = 20;    // hour (0-23)
  // Light schedule 2 (on)
  int8_t light2OnStart = 12;   // hour (0-23)
  int8_t light2OnEnd = 14;     // hour (0-23)
  // Light schedule 2 (off)
  int8_t light2OffStart = 21;  // hour (0-23)
  int8_t light2OffEnd = 23;    // hour (0-23)
  // Temperature thresholds
  float tempMin = 20.0;
  float tempMax = 28.0;
};

// Persistent storage for AquaMonitor configuration
class AquaMonitorStore {
 public:
  AquaMonitorConfig load() {
    AquaMonitorConfig cfg;
    Preferences prefs;
    prefs.begin(NAMESPACE, true);

    cfg.light1OnStart = prefs.getInt("l1os", cfg.light1OnStart);
    cfg.light1OnEnd = prefs.getInt("l1oe", cfg.light1OnEnd);
    cfg.light1OffStart = prefs.getInt("l1fs", cfg.light1OffStart);
    cfg.light1OffEnd = prefs.getInt("l1fe", cfg.light1OffEnd);
    cfg.light2OnStart = prefs.getInt("l2os", cfg.light2OnStart);
    cfg.light2OnEnd = prefs.getInt("l2oe", cfg.light2OnEnd);
    cfg.light2OffStart = prefs.getInt("l2fs", cfg.light2OffStart);
    cfg.light2OffEnd = prefs.getInt("l2fe", cfg.light2OffEnd);
    cfg.tempMin = prefs.getFloat("tmin", cfg.tempMin);
    cfg.tempMax = prefs.getFloat("tmax", cfg.tempMax);

    prefs.end();
    return cfg;
  }

  void save(const AquaMonitorConfig& cfg) {
    Preferences prefs;
    prefs.begin(NAMESPACE, false);

    prefs.putInt("l1os", cfg.light1OnStart);
    prefs.putInt("l1oe", cfg.light1OnEnd);
    prefs.putInt("l1fs", cfg.light1OffStart);
    prefs.putInt("l1fe", cfg.light1OffEnd);
    prefs.putInt("l2os", cfg.light2OnStart);
    prefs.putInt("l2oe", cfg.light2OnEnd);
    prefs.putInt("l2fs", cfg.light2OffStart);
    prefs.putInt("l2fe", cfg.light2OffEnd);
    prefs.putFloat("tmin", cfg.tempMin);
    prefs.putFloat("tmax", cfg.tempMax);

    prefs.end();
  }

 private:
  static constexpr const char* NAMESPACE = "aquamonitor";
};

// AquaMonitor device module
class AquaMonitor : public GenericModule {
 public:
  AquaMonitor(Display& display, Config& config, Alert& alert, HttpServer& httpServer,
              TimeSync& timeSync)
      : GenericModule(display, config, alert, httpServer, timeSync),
        store_(),
        config_(store_.load()),
        oneWire_(TEMPERATURE_PIN),
        sensors_(&oneWire_),
        temperature_(NAN),  // NAN until the first valid reading
        lightLevel_(0),
        lightOn_(false),
        waterLevelOk_(false) {}

  const char* getType() const override { return "aquamonitor"; }

  void begin() override {
    // Register the module configuration API endpoint
    httpServer_.webServer().on("/api/module/config", HTTP_GET,
                                [this]() { handleGetModuleConfig(); });

    // Water level switch: pulled up, the switch connects the pin to ground
    pinMode(WATER_LEVEL_PIN, INPUT_PULLUP);

    // Light sensor: photoresistor on an ADC1 pin (ADC2 is unusable while WiFi is on)
    pinMode(LIGHT_PIN, INPUT);
    analogReadResolution(12);

    // Temperature probe: do not block while the conversion is running
    sensors_.begin();
    sensors_.setWaitForConversion(false);
    sensors_.setResolution(TEMPERATURE_RESOLUTION);

    // Take a first measure as soon as update() is called
    lastWaterLevelRead_ = lastLightRead_ = lastTemperatureRequest_ = millis() - READ_PERIOD_MS;
  }

  // Each value is refreshed every READ_PERIOD_MS
  void update() override {
    const uint32_t now = millis();

    if (now - lastWaterLevelRead_ >= READ_PERIOD_MS) {
      lastWaterLevelRead_ = now;
      const int raw = digitalRead(WATER_LEVEL_PIN);
      waterLevelOk_ = (raw == WATER_LEVEL_OK_STATE);
      Serial.printf("AquaMonitor water level: pin %d -> %s\n", raw, waterLevelOk_ ? "OK" : "NOK");
    }

    if (now - lastLightRead_ >= READ_PERIOD_MS) {
      lastLightRead_ = now;
      lightLevel_ = analogRead(LIGHT_PIN);
      lightOn_ = (lightLevel_ >= LIGHT_ON_THRESHOLD);
      Serial.printf("AquaMonitor light: raw %d (threshold %d) -> %s\n", lightLevel_,
                    LIGHT_ON_THRESHOLD, lightOn_ ? "ON" : "OFF");
    }

    // Temperature is read in 2 steps: request a conversion, then read the result
    // once it is complete (up to 750ms), without blocking the loop.
    if (!temperatureRequested_) {
      if (now - lastTemperatureRequest_ >= READ_PERIOD_MS) {
        lastTemperatureRequest_ = now;
        sensors_.requestTemperatures();
        temperatureRequested_ = true;
        Serial.println("AquaMonitor temperature: conversion requested");
      }
    } else if (sensors_.isConversionComplete()) {
      temperatureRequested_ = false;
      float t = sensors_.getTempCByIndex(0);
      temperature_ = (t == DEVICE_DISCONNECTED_C) ? NAN : t;
      if (isnan(temperature_)) {
        Serial.println("AquaMonitor temperature: probe not found or read error");
      } else {
        Serial.printf("AquaMonitor temperature: %.2f C\n", temperature_);
      }
    }
  }

  JsonDocument getStatus() override {
    JsonDocument doc;
    if (isnan(temperature_)) {
      doc["temperature"] = nullptr;
    } else {
      doc["temperature"] = temperature_;
    }
    doc["lightLevel"] = lightLevel_;
    doc["lightOn"] = lightOn_;
    doc["waterLevelOk"] = waterLevelOk_;
    return doc;
  }

  String getStatusHtml() override {
    String html;
    html += "<tr><td>Temperature</td><td id=\"temp\" class=\"\">-</td></tr>\n";
    html += "<tr><td>Light</td><td id=\"light\" class=\"\">-</td></tr>\n";
    html += "<tr><td>Water Level</td><td id=\"water\" class=\"\">-</td></tr>\n";
    html += "<script>\n";
    html += "function moduleStatus(d) {\n";
    html += "  const temp = document.getElementById('temp');\n";
    html += "  const tmin = " + String(config_.tempMin) + ";\n";
    html += "  const tmax = " + String(config_.tempMax) + ";\n";
    html += "  if (d.temperature === null) { temp.textContent = 'N/A'; temp.className = 'nok'; }\n";
    html += "  else {\n";
    html += "    temp.textContent = d.temperature.toFixed(1) + '°C';\n";
    html += "    if (d.temperature < tmin || d.temperature > tmax) temp.className = 'nok';\n";
    html += "    else temp.className = 'ok';\n";
    html += "  }\n";
    html += "\n";
    html += "  const light = document.getElementById('light');\n";
    html += "  light.textContent = d.lightOn ? 'ON' : 'OFF';\n";
    html += "  light.className = d.lightOn ? 'ok' : '';\n";
    html += "\n";
    html += "  const water = document.getElementById('water');\n";
    html += "  water.textContent = d.waterLevelOk ? 'OK' : 'NOK';\n";
    html += "  water.className = d.waterLevelOk ? 'ok' : 'nok';\n";
    html += "}\n";
    html += "</script>\n";
    return html;
  }

  String getConfigHtml() override {
    String html;
    html += "<h2>Light Schedules</h2>\n";
    html += "<div class=\"row\">\n";
    html += "  <label>Light ON Schedule 1 Start (hour)<input id=\"l1os\" type=\"number\" min=\"0\" max=\"23\" value=\"" +
            String(config_.light1OnStart) + "\" required></label>\n";
    html += "  <label>End (hour)<input id=\"l1oe\" type=\"number\" min=\"0\" max=\"23\" value=\"" +
            String(config_.light1OnEnd) + "\" required></label>\n";
    html += "</div>\n";
    html += "<div class=\"row\">\n";
    html += "  <label>Light OFF Schedule 1 Start (hour)<input id=\"l1fs\" type=\"number\" min=\"0\" max=\"23\" value=\"" +
            String(config_.light1OffStart) + "\" required></label>\n";
    html += "  <label>End (hour)<input id=\"l1fe\" type=\"number\" min=\"0\" max=\"23\" value=\"" +
            String(config_.light1OffEnd) + "\" required></label>\n";
    html += "</div>\n";
    html += "<div class=\"row\">\n";
    html += "  <label>Light ON Schedule 2 Start (hour)<input id=\"l2os\" type=\"number\" min=\"0\" max=\"23\" value=\"" +
            String(config_.light2OnStart) + "\" required></label>\n";
    html += "  <label>End (hour)<input id=\"l2oe\" type=\"number\" min=\"0\" max=\"23\" value=\"" +
            String(config_.light2OnEnd) + "\" required></label>\n";
    html += "</div>\n";
    html += "<div class=\"row\">\n";
    html += "  <label>Light OFF Schedule 2 Start (hour)<input id=\"l2fs\" type=\"number\" min=\"0\" max=\"23\" value=\"" +
            String(config_.light2OffStart) + "\" required></label>\n";
    html += "  <label>End (hour)<input id=\"l2fe\" type=\"number\" min=\"0\" max=\"23\" value=\"" +
            String(config_.light2OffEnd) + "\" required></label>\n";
    html += "</div>\n";
    html += "<h2>Temperature Thresholds</h2>\n";
    html += "<label>Minimum Temperature (°C)<input id=\"tmin\" type=\"number\" step=\"0.1\" value=\"" +
            String(config_.tempMin) + "\" required></label>\n";
    html += "<label>Maximum Temperature (°C)<input id=\"tmax\" type=\"number\" step=\"0.1\" value=\"" +
            String(config_.tempMax) + "\" required></label>\n";
    html += "<button type=\"button\" id=\"save-aqua-btn\">Save Configuration</button>\n";
    html += "<p id=\"aqua-msg\"></p>\n";
    html += "<script>\n";
    html += "document.getElementById('save-aqua-btn').addEventListener('click', async () => {\n";
    html += "  const body = {\n";
    html += "    light1OnStart: parseInt(document.getElementById('l1os').value),\n";
    html += "    light1OnEnd: parseInt(document.getElementById('l1oe').value),\n";
    html += "    light1OffStart: parseInt(document.getElementById('l1fs').value),\n";
    html += "    light1OffEnd: parseInt(document.getElementById('l1fe').value),\n";
    html += "    light2OnStart: parseInt(document.getElementById('l2os').value),\n";
    html += "    light2OnEnd: parseInt(document.getElementById('l2oe').value),\n";
    html += "    light2OffStart: parseInt(document.getElementById('l2fs').value),\n";
    html += "    light2OffEnd: parseInt(document.getElementById('l2fe').value),\n";
    html += "    tempMin: parseFloat(document.getElementById('tmin').value),\n";
    html += "    tempMax: parseFloat(document.getElementById('tmax').value)\n";
    html += "  };\n";
    html += "  try {\n";
    html += "    const r = await fetch('/api/module/config', {\n";
    html += "      method: 'POST',\n";
    html += "      headers: {'Content-Type': 'application/json'},\n";
    html += "      body: JSON.stringify(body)\n";
    html += "    });\n";
    html += "    const d = await r.json();\n";
    html += "    if (!r.ok) throw new Error(d.error || ('HTTP ' + r.status));\n";
    html += "    document.getElementById('aqua-msg').textContent = 'Configuration saved successfully.';\n";
    html += "    document.getElementById('aqua-msg').className = 'ok';\n";
    html += "  } catch (err) {\n";
    html += "    document.getElementById('aqua-msg').textContent = 'Save failed: ' + err.message;\n";
    html += "    document.getElementById('aqua-msg').className = 'nok';\n";
    html += "  }\n";
    html += "});\n";
    html += "</script>\n";
    return html;
  }

  bool saveConfig(JsonObjectConst cfg, String& error) override {
    // Validate and extract configuration fields
    if (!cfg["light1OnStart"].is<int>() || !cfg["light1OnEnd"].is<int>() ||
        !cfg["light1OffStart"].is<int>() || !cfg["light1OffEnd"].is<int>() ||
        !cfg["light2OnStart"].is<int>() || !cfg["light2OnEnd"].is<int>() ||
        !cfg["light2OffStart"].is<int>() || !cfg["light2OffEnd"].is<int>() ||
        !cfg["tempMin"].is<float>() || !cfg["tempMax"].is<float>()) {
      error = "Missing or invalid configuration fields";
      return false;
    }

    AquaMonitorConfig newConfig;
    newConfig.light1OnStart = cfg["light1OnStart"].as<int>();
    newConfig.light1OnEnd = cfg["light1OnEnd"].as<int>();
    newConfig.light1OffStart = cfg["light1OffStart"].as<int>();
    newConfig.light1OffEnd = cfg["light1OffEnd"].as<int>();
    newConfig.light2OnStart = cfg["light2OnStart"].as<int>();
    newConfig.light2OnEnd = cfg["light2OnEnd"].as<int>();
    newConfig.light2OffStart = cfg["light2OffStart"].as<int>();
    newConfig.light2OffEnd = cfg["light2OffEnd"].as<int>();
    newConfig.tempMin = cfg["tempMin"].as<float>();
    newConfig.tempMax = cfg["tempMax"].as<float>();

    // Validate ranges
    if (newConfig.light1OnStart < 0 || newConfig.light1OnStart > 23 ||
        newConfig.light1OnEnd < 0 || newConfig.light1OnEnd > 23 ||
        newConfig.light1OffStart < 0 || newConfig.light1OffStart > 23 ||
        newConfig.light1OffEnd < 0 || newConfig.light1OffEnd > 23 ||
        newConfig.light2OnStart < 0 || newConfig.light2OnStart > 23 ||
        newConfig.light2OnEnd < 0 || newConfig.light2OnEnd > 23 ||
        newConfig.light2OffStart < 0 || newConfig.light2OffStart > 23 ||
        newConfig.light2OffEnd < 0 || newConfig.light2OffEnd > 23) {
      error = "Hour values must be between 0 and 23";
      return false;
    }

    if (newConfig.tempMin >= newConfig.tempMax) {
      error = "Minimum temperature must be less than maximum temperature";
      return false;
    }

    // Save the configuration
    config_ = newConfig;
    store_.save(config_);
    return true;
  }

 private:
  void handleGetModuleConfig() {
    JsonDocument doc;
    doc["light1OnStart"] = config_.light1OnStart;
    doc["light1OnEnd"] = config_.light1OnEnd;
    doc["light1OffStart"] = config_.light1OffStart;
    doc["light1OffEnd"] = config_.light1OffEnd;
    doc["light2OnStart"] = config_.light2OnStart;
    doc["light2OnEnd"] = config_.light2OnEnd;
    doc["light2OffStart"] = config_.light2OffStart;
    doc["light2OffEnd"] = config_.light2OffEnd;
    doc["tempMin"] = config_.tempMin;
    doc["tempMax"] = config_.tempMax;
    httpServer_.sendJson(200, doc);
  }

  // GPIO assignment
  static constexpr uint8_t WATER_LEVEL_PIN = 25;  // digital, level switch
  static constexpr uint8_t LIGHT_PIN = 34;        // analog (ADC1), photoresistor
  static constexpr uint8_t TEMPERATURE_PIN = 4;   // OneWire, temperature probe

  static constexpr uint32_t READ_PERIOD_MS = 5000;
  static constexpr uint8_t WATER_LEVEL_OK_STATE = HIGH;  // pin state when water level is ok
  static constexpr int LIGHT_ON_THRESHOLD = 1000;  // raw ADC value (0-4095) above which light is on
  static constexpr uint8_t TEMPERATURE_RESOLUTION = 12;  // bits

  AquaMonitorStore store_;
  AquaMonitorConfig config_;

  OneWire oneWire_;
  DallasTemperature sensors_;

  // Status variables, refreshed by update()
  float temperature_;
  int lightLevel_;
  bool lightOn_;
  bool waterLevelOk_;

  uint32_t lastWaterLevelRead_ = 0;
  uint32_t lastLightRead_ = 0;
  uint32_t lastTemperatureRequest_ = 0;
  bool temperatureRequested_ = false;
};
