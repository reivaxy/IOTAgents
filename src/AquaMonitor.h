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
        config_(),  // defaults, replaced by the stored values in begin()
        oneWire_(TEMPERATURE_PIN),
        tempSensor_(&oneWire_),
        temperature_(NAN),  // NAN until the first valid reading
        lightLevel_(0),
        lightOn_(false),
        waterLevelOk_(false) {}

  const char* getType() const override { return "aquamonitor"; }

  void begin() override {
    // The module is a global object: its constructor runs before NVS is initialised, so the
    // stored configuration can only be read here.
    config_ = store_.load();

    // Register the module configuration API endpoint
    httpServer_.webServer().on("/api/module/config", HTTP_GET,
                                [this]() { handleGetModuleConfig(); });

    // Water level switch: pulled up, the switch connects the pin to ground
    pinMode(WATER_LEVEL_PIN, INPUT_PULLUP);

    // Light sensor: photoresistor on an ADC1 pin (ADC2 is unusable while WiFi is on)
    pinMode(LIGHT_PIN, INPUT);
    analogReadResolution(12);

    // Temperature probe: do not block while the conversion is running
    tempSensor_.begin();
    tempSensor_.setWaitForConversion(false);
    findTemperatureProbe();

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
      checkAlert(!waterLevelOk_, waterAlertSent_, waterAlert_, "AquaMonitor: water level too low");
    }

    if (now - lastLightRead_ >= READ_PERIOD_MS) {
      lastLightRead_ = now;
      lightLevel_ = analogRead(LIGHT_PIN);
      lightOn_ = (lightLevel_ >= LIGHT_ON_THRESHOLD);
      Serial.printf("AquaMonitor light: raw %d (threshold %d) -> %s\n", lightLevel_,
                    LIGHT_ON_THRESHOLD, lightOn_ ? "ON" : "OFF");
      checkLightAlert();
    }

    // Temperature is read in 2 steps: request a conversion, then read the result
    // once it is complete (up to 750ms), without blocking the loop.
    if (!temperatureRequested_) {
      if (now - lastTemperatureRequest_ >= READ_PERIOD_MS) {
        lastTemperatureRequest_ = now;
        // Retry the probe detection until it is found (wiring fixed, probe plugged later...)
        if (!tempProbeFound_ && !findTemperatureProbe()) {
          temperature_ = NAN;
          checkTemperatureAlert();
          return;
        }
        tempSensor_.requestTemperaturesByAddress(tempAddr_);
        temperatureRequested_ = true;
        Serial.println("AquaMonitor temperature: conversion requested");
      }
    } else if (tempSensor_.isConversionComplete()) {
      temperatureRequested_ = false;
      float t = tempSensor_.getTempC(tempAddr_);
      temperature_ = (t == DEVICE_DISCONNECTED_C) ? NAN : t;
      if (isnan(temperature_)) {
        Serial.println("AquaMonitor temperature: probe not found or read error");
      } else {
        Serial.printf("AquaMonitor temperature: %.2f C\n", temperature_);
      }
      checkTemperatureAlert();
    }

    // if (now - lastTemperatureRequest_ >= READ_PERIOD_MS) {
    //   lastTemperatureRequest_ = now;
    // }
  }

  JsonDocument getStatus() override {
    // Each entry is {"value": ..., "alertMsg": ""}: alertMsg is empty when there is no anomaly,
    // otherwise it contains the cause. The root "alert" is true when at least one entry has one.
    JsonDocument doc;
    doc["alert"] = !tempAlert_.isEmpty() || !lightAlert_.isEmpty() || !waterAlert_.isEmpty();
    if (isnan(temperature_)) {
      doc["temperature"]["value"] = nullptr;
    } else {
      doc["temperature"]["value"] = temperature_;
    }
    doc["temperature"]["alertMsg"] = tempAlert_;
    doc["lightLevel"]["value"] = lightLevel_;
    doc["lightLevel"]["alertMsg"] = "";
    doc["lightOn"]["value"] = lightOn_;
    doc["lightOn"]["alertMsg"] = lightAlert_;
    doc["waterLevelOk"]["value"] = waterLevelOk_;
    doc["waterLevelOk"]["alertMsg"] = waterAlert_;
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
    html += "  temp.textContent = d.temperature.value === null ? 'N/A' : d.temperature.value.toFixed(1) + '°C';\n";
    html += "  temp.className = d.temperature.alertMsg ? 'nok' : 'ok';\n";
    html += "  temp.title = d.temperature.alertMsg;\n";
    html += "\n";
    html += "  const light = document.getElementById('light');\n";
    html += "  light.textContent = d.lightOn.value ? 'ON' : 'OFF';\n";
    html += "  light.className = d.lightOn.alertMsg ? 'nok' : (d.lightOn.value ? 'ok' : '');\n";
    html += "  light.title = d.lightOn.alertMsg;\n";
    html += "\n";
    html += "  const water = document.getElementById('water');\n";
    html += "  water.textContent = d.waterLevelOk.value ? 'OK' : 'NOK';\n";
    html += "  water.className = d.waterLevelOk.alertMsg ? 'nok' : 'ok';\n";
    html += "  water.title = d.waterLevelOk.alertMsg;\n";
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
  // Sends `text` once per anomaly: the sent flag is set only when SmsRelay accepted the alert
  // (otherwise it is retried at the next measure) and cleared when the value is ok again.
  // `cause` is exposed in the status: `text` during an anomaly, empty otherwise.
  void checkAlert(bool anomaly, bool& sent, String& cause, const String& text) {
    cause = anomaly ? text : String();
    if (!anomaly) {
      sent = false;
    } else if (!sent) {
      sent = alert_.send(text);
    }
  }

  void checkTemperatureAlert() {
    if (isnan(temperature_)) {
      checkAlert(true, tempAlertSent_, tempAlert_, "AquaMonitor: temperature probe not responding");
      return;
    }

    // Keep an anomaly active until the reading is 0.4 C back inside its threshold.
    // Track it independently of delivery so failed sends still retry in the hysteresis band.
    if (temperature_ < config_.tempMin) {
      tempAlertState_ = TemperatureAlertState::TooLow;
    } else if (temperature_ > config_.tempMax) {
      tempAlertState_ = TemperatureAlertState::TooHigh;
    } else if ((tempAlertState_ == TemperatureAlertState::TooLow &&
                temperature_ >= config_.tempMin + TEMPERATURE_HYSTERESIS_C) ||
               (tempAlertState_ == TemperatureAlertState::TooHigh &&
                temperature_ <= config_.tempMax - TEMPERATURE_HYSTERESIS_C)) {
      tempAlertState_ = TemperatureAlertState::Normal;
    }

    if (tempAlertState_ == TemperatureAlertState::TooLow) {
      checkAlert(true, tempAlertSent_, tempAlert_,
                 "AquaMonitor: temperature too low (" + String(temperature_, 1) + " C)");
    } else if (tempAlertState_ == TemperatureAlertState::TooHigh) {
      checkAlert(true, tempAlertSent_, tempAlert_,
                 "AquaMonitor: temperature too high (" + String(temperature_, 1) + " C)");
    } else {
      checkAlert(false, tempAlertSent_, tempAlert_, "");
    }
  }

  // True when `hour` is in [start, end[ (the range may wrap around midnight).
  static bool hourInRange(int hour, int start, int end) {
    return start <= end ? (hour >= start && hour < end) : (hour >= start || hour < end);
  }

  // The "on" windows must see the light ON and the "off" windows must see it OFF. Windows are
  // [start, end[: the end hour is not checked (8 to 10 covers 08:00 to 09:59). Outside the
  // windows the state is not restricted: no alert, flag unchanged.
  void checkLightAlert() {
    if (!timeSync_.isSynced() || !timeSync_.hasUtcOffset()) {
      lightAlert_ = "";
      return;  // the expected state is unknown without the local time
    }
    const time_t now = timeSync_.now();
    tm local;
    localtime_r(&now, &local);
    const int h = local.tm_hour;

    const bool mustBeOn = hourInRange(h, config_.light1OnStart, config_.light1OnEnd) ||
                          hourInRange(h, config_.light2OnStart, config_.light2OnEnd);
    const bool mustBeOff = hourInRange(h, config_.light1OffStart, config_.light1OffEnd) ||
                           hourInRange(h, config_.light2OffStart, config_.light2OffEnd);

    if (mustBeOn == mustBeOff) {
      lightAlert_ = "";
      return;  // not restricted (or contradictory windows): no alert, flag unchanged
    }

    if (mustBeOn && !lightOn_) {
      checkAlert(true, lightAlertSent_, lightAlert_, "AquaMonitor: light is OFF but should be ON");
    } else if (mustBeOff && lightOn_) {
      checkAlert(true, lightAlertSent_, lightAlert_, "AquaMonitor: light is ON but should be OFF");
    } else {
      checkAlert(false, lightAlertSent_, lightAlert_, "");  // state matches the restriction
    }
  }

  // Look for a DS18B20 on the OneWire bus. Returns true when found.
  bool findTemperatureProbe() {
    tempProbeFound_ = false;

    // reset() returns 1 only if a device answers with a presence pulse
    if (!oneWire_.reset()) {
      Serial.printf(
          "AquaMonitor temperature: no presence pulse on GPIO %d "
          "(check wiring, 3.3V power and the 4.7k pull-up between DATA and 3.3V)\n",
          TEMPERATURE_PIN);
      return false;
    }

    tempSensor_.begin();  // re-enumerate the bus
    if (!tempSensor_.getAddress(tempAddr_, 0)) {
      Serial.println("AquaMonitor temperature probe not found (device present but not a DS18B20?)");
      return false;
    }

    tempSensor_.setResolution(tempAddr_, TEMPERATURE_RESOLUTION);
    tempSensor_.setWaitForConversion(false);
    tempProbeFound_ = true;
    Serial.printf("AquaMonitor temperature probe found: %02X%02X%02X%02X%02X%02X%02X%02X\n",
                  tempAddr_[0], tempAddr_[1], tempAddr_[2], tempAddr_[3], tempAddr_[4],
                  tempAddr_[5], tempAddr_[6], tempAddr_[7]);
    return true;
  }

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
  static constexpr float TEMPERATURE_HYSTERESIS_C = 0.4f;

  enum class TemperatureAlertState { Normal, TooLow, TooHigh };
  TemperatureAlertState tempAlertState_ = TemperatureAlertState::Normal;

  AquaMonitorStore store_;
  AquaMonitorConfig config_;

  OneWire oneWire_;
  DallasTemperature tempSensor_;

  // Status variables, refreshed by update()
  float temperature_;
  int lightLevel_;
  bool lightOn_;
  bool waterLevelOk_;

  uint32_t lastWaterLevelRead_ = 0;
  uint32_t lastLightRead_ = 0;
  uint32_t lastTemperatureRequest_ = 0;
  bool temperatureRequested_ = false;
  DeviceAddress tempAddr_ = {0};
  bool tempProbeFound_ = false;

  // An alert has been sent for the current anomaly (reset when the value is ok again)
  bool tempAlertSent_ = false;
  bool lightAlertSent_ = false;
  bool waterAlertSent_ = false;

  // Cause of the current anomaly (empty when none), exposed in the status
  String tempAlert_;
  String lightAlert_;
  String waterAlert_;
};
