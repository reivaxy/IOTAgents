#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "Platform.h"
#include <WiFiClient.h>

#include "Alert.h"
#include "Config.h"
#include "GenericModule.h"

#define IP_START 1
#define IP_END 255

// Finds the SmsRelay module on the home network (/24 subnet of this module) by calling
// /api/ping (POST, JSON {"IP", "name"}) on x.x.x.0 .. x.x.x.255 until the answer is "SmsRelay", then stores the IP.
// Non blocking by steps: update() probes a single address per call, so it must be called
// from loop(). Once connected to the home network, a stored SmsRelay IP is checked with
// /api/ping; the scan starts automatically if none is stored or if the stored one does not
// answer. start() forces a new scan. Once the SmsRelay is known, it is pinged every
// PING_INTERVAL_MS; a failed ping makes isNotFound() true until a later ping succeeds.
class SmsRelayScanner {
 public:
  static constexpr uint32_t SAVE_CHECK_TIMEOUT_MS = 10000;  // connect and read, check on config save
  static constexpr uint32_t PING_INTERVAL_MS = 10UL * 60 * 1000;

  void begin(Config& config, ConfigStore& store, Alert& alert, const GenericModule& module) {
    config_ = &config;
    store_ = &store;
    alert_ = &alert;
    module_ = &module;
  }

  // Starts (or restarts) a scan. Returns false if not connected to the home network.
  bool start() {
    if (!isStaConnected()) return false;
    if (usingFallbackWifi_) return verify(FALLBACK_AP_IP);
    prefix_ = WiFi.localIP();
    next_ = IP_START;
    scanning_ = true;
    Serial.printf("SmsRelay scan started on %u.%u.%u.x (self %s, mask %s, gateway %s, RSSI %d dBm)\n",
                  prefix_[0], prefix_[1], prefix_[2], WiFi.localIP().toString().c_str(),
                  WiFi.subnetMask().toString().c_str(), WiFi.gatewayIP().toString().c_str(),
                  WiFi.RSSI());
    logReachability(WiFi.gatewayIP(), 80);
    return true;
  }

  bool isScanning() const { return scanning_; }

  // True when no SmsRelay is known: either a scan ended without leaving any IP stored, or the
  // last manually saved IP did not answer, or the last periodic ping failed. Never true while
  // scanning.
  bool isNotFound() const {
    return !scanning_ &&
           ((!usingFallbackWifi_ && autoChecked_ && config_->smsRelayIp.isEmpty()) ||
            manualCheckFailed_ || pingFailed_);
  }

  void stop() { finish(); }

  // A new network needs a fresh ping with the module's new STA address.
  void onWifiConnected(bool fallback) {
    finish();
    usingFallbackWifi_ = fallback;
    autoChecked_ = false;
    manualCheckFailed_ = false;
    pingFailed_ = false;
    update();
  }

  // Calls /api/ping on a manually entered IP and remembers whether SmsRelay answered (this drives
  // isNotFound()). An empty IP clears the failure. Nothing is checked (true is returned) when not
  // connected to the home network. Blocks for up to twice timeoutMs (connect + read).
  bool verify(const String& ip, uint32_t timeoutMs = SCAN_TIMEOUT_MS) {
    pingFailed_ = false;  // the IP may have changed: the periodic ping state no longer applies
    lastPing_ = millis();
    const String target = usingFallbackWifi_ ? String(FALLBACK_AP_IP) : ip;
    if (target.isEmpty()) {
      manualCheckFailed_ = false;
      return true;
    }
    if (!isStaConnected()) return true;

    IPAddress addr;
    if (!addr.fromString(target)) return false;
    const String url = pingUrl(addr);
    Serial.printf("SmsRelay check: %s (self %s, mask %s, gateway %s)\n", url.c_str(),
                  WiFi.localIP().toString().c_str(), WiFi.subnetMask().toString().c_str(),
                  WiFi.gatewayIP().toString().c_str());
    const bool ok = probe(url, timeoutMs);
    manualCheckFailed_ = !ok;
    if (!ok) logReachability(addr, 80);
    Serial.printf("SmsRelay check: %s\n", ok ? "answered" : "no answer");
    return ok;
  }

  static bool isStaConnected() {
    return WiFi.getMode() == WIFI_STA && WiFi.status() == WL_CONNECTED;
  }

  void update() {
    if (!isStaConnected()) return;

    // Never scan the fallback subnet or use/persist the home-network relay IP here.
    if (usingFallbackWifi_) {
      if (!autoChecked_ || millis() - lastPing_ >= PING_INTERVAL_MS) {
        autoChecked_ = true;
        verify(FALLBACK_AP_IP);
      }
      return;
    }

    if (!autoChecked_) {
      autoChecked_ = true;
      if (config_->smsRelayIp.isEmpty()) {
        start();
      } else {
        Serial.printf("SmsRelay IP stored: %s, checking it\n", config_->smsRelayIp.c_str());
        if (verify(config_->smsRelayIp)) {
          alert_->send(CONNECTED_MESSAGE);
        } else {
          Serial.println("Stored SmsRelay IP does not answer, scanning the network");
          start();
        }
      }
      lastPing_ = millis();
    }

    if (!scanning_ && !config_->smsRelayIp.isEmpty() &&
        millis() - lastPing_ >= PING_INTERVAL_MS) {
      lastPing_ = millis();
      IPAddress addr;
      if (addr.fromString(config_->smsRelayIp)) {
        const String url = pingUrl(addr);
        Serial.printf("SmsRelay periodic ping: %s\n", url.c_str());
        pingFailed_ = !probe(url, SCAN_TIMEOUT_MS);
        if (!pingFailed_) manualCheckFailed_ = false;  // the SmsRelay answers: clear any older failure
        Serial.printf("SmsRelay periodic ping: %s\n", pingFailed_ ? "no answer" : "answered");
      }
    }

    if (!scanning_) return;

    const IPAddress self = WiFi.localIP();
    const IPAddress candidate(prefix_[0], prefix_[1], prefix_[2], static_cast<uint8_t>(next_));

    const String url = pingUrl(candidate);
    Serial.printf("SmsRelay scan: %s%s\n", url.c_str(), candidate == self ? " (own address, skipped)" : "");
    if (candidate != self && probe(url, SCAN_TIMEOUT_MS)) {
      config_->smsRelayIp = candidate.toString();
      store_->saveSmsRelayIp(*config_);
      manualCheckFailed_ = false;
      pingFailed_ = false;
      lastPing_ = millis();
      finish();
      Serial.printf("SmsRelay found at %s\n", config_->smsRelayIp.c_str());
      alert_->send(CONNECTED_MESSAGE);
      return;
    }

    if (++next_ > IP_END) {
      finish();
      Serial.println("SmsRelay scan finished: not found");
    }
  }

 private:
  static constexpr uint32_t SCAN_TIMEOUT_MS = 2000;  // connect and read, scan and startup check
  static constexpr const char* PING_PATH = "/api/ping";
  static constexpr const char* FALLBACK_AP_IP = "192.168.4.1";
  static constexpr const char* EXPECTED_ANSWER = "SmsRelay";
  static constexpr const char* CONNECTED_MESSAGE = "module connected";

  Config* config_ = nullptr;
  ConfigStore* store_ = nullptr;
  Alert* alert_ = nullptr;
  const GenericModule* module_ = nullptr;
  bool usingFallbackWifi_ = false;
  bool autoChecked_ = false;
  bool scanning_ = false;
  bool manualCheckFailed_ = false;
  bool pingFailed_ = false;
  uint32_t lastPing_ = 0;
  IPAddress prefix_;
  uint16_t next_ = IP_START;

  void finish() {
    if (!scanning_) return;
    scanning_ = false;
  }

  // Plain http (never https): the client used below is a non secure WiFiClient.
  static String pingUrl(const IPAddress& ip) { return String("http://") + ip.toString() + PING_PATH; }

  // Diagnostic only: a live host answers a SYN within milliseconds (SYN-ACK, or RST when the port
  // is closed); "no answer" means the packets are lost on the way.
  static void logReachability(const IPAddress& ip, uint16_t port) {
    WiFiClient client;
    const uint32_t startMs = millis();
  #if defined(ESP8266)
    client.setTimeout(2000);
    const bool connected = client.connect(ip, port);
  #else
    const bool connected = client.connect(ip, port, 2000);
  #endif
    const uint32_t elapsedMs = millis() - startMs;
    client.stop();
    Serial.printf("  TCP test %s:%u -> %s (%lu ms)\n", ip.toString().c_str(), port,
                  connected ? "connected" : (elapsedMs < 1500 ? "refused, host alive" : "no answer"),
                  static_cast<unsigned long>(elapsedMs));
  }

  bool probe(const String& url, uint32_t timeoutMs) {
    // The ping tells SmsRelay who is calling:
    // {"IP": <this module IP>, "name": <module name>, "type": <module type>}.
    JsonDocument doc;
    doc["IP"] = WiFi.localIP().toString();
    doc["name"] = config_->name;
    doc["type"] = module_ != nullptr ? module_->getType() : "generic";
    String payload;
    serializeJson(doc, payload);
    Serial.printf("  ping POST %s %s\n", url.c_str(), payload.c_str());

    WiFiClient client;
    HTTPClient http;
    setHttpConnectTimeout(client, http, timeoutMs);
    http.setTimeout(timeoutMs);
    http.setReuse(false);  // one request per connection: "Connection: close", socket closed by end()
    if (!http.begin(client, url)) {
      return false;
    }
    http.addHeader("Content-Type", "application/json");

    bool found = false;
    const int code = http.POST(payload);
    if (code == HTTP_CODE_OK) {
      String body = http.getString();
      body.trim();
      
      // Try to parse response as JSON: {"name":"SmsRelay","ssid":"...","password":"..."}
      JsonDocument response;
      if (deserializeJson(response, body) == DeserializationError::Ok) {
        const char* name = response["name"];
        if (name != nullptr && strcmp(name, EXPECTED_ANSWER) == 0) {
          found = true;
          // Save fallback WiFi credentials if present
          const char* ssid = response["ssid"];
          const char* password = response["password"];
          if (ssid != nullptr && password != nullptr) {
            config_->fallbackSsid = ssid;
            config_->fallbackPwd = password;
            store_->saveFallbackWifi(*config_);
            Serial.printf("  Fallback WiFi saved: SSID='%s'\n", ssid);
          }
          Serial.printf("  ping HTTP 200, SmsRelay found\n");
        } else {
          Serial.printf("  ping HTTP 200, body (%u bytes): '%s' -> not the expected answer\n", 
                        body.length(), body.c_str());
        }
      } else {
        Serial.printf("  ping HTTP 200, invalid JSON response (%u bytes): '%s'\n", 
                      body.length(), body.c_str());
      }
    } else {
      Serial.printf("  ping failed: HTTP %d (%s)\n", code, HTTPClient::errorToString(code).c_str());
    }
    http.end();
    client.stop();  // explicit close, whatever the outcome (timeout, error or answer)
    return found;
  }
};
