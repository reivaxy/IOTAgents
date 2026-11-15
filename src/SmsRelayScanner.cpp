#include "SmsRelayScanner.h"

#include "Platform.h"
#include <WiFiClient.h>

#include "Alert.h"
#include "Config.h"

void SmsRelayScanner::begin(Config& config, ConfigStore& store, Alert& alert, StatusProvider statusProvider) {
  config_ = &config;
  store_ = &store;
  alert_ = &alert;
  statusProvider_ = statusProvider;
}

bool SmsRelayScanner::start() {
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

bool SmsRelayScanner::isScanning() const { return scanning_; }

bool SmsRelayScanner::isNotFound() const {
  return !scanning_ &&
         ((!usingFallbackWifi_ && autoChecked_ && config_->smsRelayIp.isEmpty()) ||
          manualCheckFailed_ || pingFailed_);
}

void SmsRelayScanner::stop() { finish(); }

void SmsRelayScanner::onWifiConnected(bool fallback) {
  finish();
  usingFallbackWifi_ = fallback;
  autoChecked_ = false;
  manualCheckFailed_ = false;
  pingFailed_ = false;
  update();
}

bool SmsRelayScanner::verify(const String& ip, uint32_t timeoutMs) {
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

bool SmsRelayScanner::isStaConnected() {
  return WiFi.getMode() == WIFI_STA && WiFi.status() == WL_CONNECTED;
}

void SmsRelayScanner::update() {
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

void SmsRelayScanner::finish() {
  if (!scanning_) return;
  scanning_ = false;
}

String SmsRelayScanner::pingUrl(const IPAddress& ip) { return String("http://") + ip.toString() + PING_PATH; }

void SmsRelayScanner::logReachability(const IPAddress& ip, uint16_t port) {
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

bool SmsRelayScanner::probe(const String& url, uint32_t timeoutMs) {
  // Share the status builder with /api/status, including device-specific fields.
  JsonDocument doc = statusProvider_();
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