#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>

#include "Config.h"

#define IP_START 140
#define IP_END 150

// Finds the SmsRelay module on the home network (/24 subnet of this module) by calling
// /api/ping on x.x.x.0 .. x.x.x.255 until the answer is "SmsRelay", then stores the IP.
// Non blocking by steps: update() probes a single address per call, so it must be called
// from loop(). Once connected to the home network, a stored SmsRelay IP is checked with
// /api/ping; the scan starts automatically if none is stored or if the stored one does not
// answer. start() forces a new scan.
class SmsRelayScanner {
 public:
  void begin(Config& config, ConfigStore& store) {
    config_ = &config;
    store_ = &store;
  }

  // Starts (or restarts) a scan. Returns false if not connected to the home network.
  bool start() {
    if (!isStaConnected()) return false;
    prefix_ = WiFi.localIP();
    next_ = IP_START;
    scanning_ = true;
    // Modem sleep delays ARP replies and SYN-ACKs (buffered by the AP until the next beacon),
    // which makes short connect timeouts fail even for hosts that are up.
    WiFi.setSleep(false);
    Serial.printf("SmsRelay scan started on %u.%u.%u.x\n", prefix_[0], prefix_[1], prefix_[2]);
    return true;
  }

  bool isScanning() const { return scanning_; }

  // True when no SmsRelay is known: either a scan ended without leaving any IP stored, or the
  // last manually saved IP did not answer. Never true while scanning.
  bool isNotFound() const {
    return !scanning_ && ((autoChecked_ && config_->smsRelayIp.isEmpty()) || manualCheckFailed_);
  }

  void stop() { finish(); }

  // Calls /api/ping on a manually entered IP and remembers whether SmsRelay answered (this drives
  // isNotFound()). An empty IP clears the failure. Nothing is checked (true is returned) when not
  // connected to the home network. Blocks for up to the connect + read timeouts.
  bool verify(const String& ip) {
    if (ip.isEmpty()) {
      manualCheckFailed_ = false;
      return true;
    }
    if (!isStaConnected()) return true;

    IPAddress addr;
    if (!addr.fromString(ip)) return false;
    const String url = pingUrl(addr);
    Serial.printf("SmsRelay check: %s\n", url.c_str());
    const bool ok = probe(url);
    manualCheckFailed_ = !ok;
    Serial.printf("SmsRelay check: %s\n", ok ? "answered" : "no answer");
    return ok;
  }

  static bool isStaConnected() {
    return WiFi.getMode() == WIFI_STA && WiFi.status() == WL_CONNECTED;
  }

  void update() {
    if (!isStaConnected()) return;

    if (!autoChecked_) {
      autoChecked_ = true;
      if (config_->smsRelayIp.isEmpty()) {
        start();
      } else {
        Serial.printf("SmsRelay IP stored: %s, checking it\n", config_->smsRelayIp.c_str());
        if (!verify(config_->smsRelayIp)) {
          Serial.println("Stored SmsRelay IP does not answer, scanning the network");
          start();
        }
      }
    }

    if (!scanning_) return;

    const IPAddress self = WiFi.localIP();
    const IPAddress candidate(prefix_[0], prefix_[1], prefix_[2], static_cast<uint8_t>(next_));

    const String url = pingUrl(candidate);
    Serial.printf("SmsRelay scan: %s%s\n", url.c_str(), candidate == self ? " (own address, skipped)" : "");
    if (candidate != self && probe(url)) {
      config_->smsRelayIp = candidate.toString();
      store_->saveSmsRelayIp(*config_);
      manualCheckFailed_ = false;
      finish();
      Serial.printf("SmsRelay found at %s\n", config_->smsRelayIp.c_str());
      return;
    }

    if (++next_ > IP_END) {
      finish();
      Serial.println("SmsRelay scan finished: not found");
    }
  }

 private:
  static constexpr uint32_t CONNECT_TIMEOUT_MS = 1000;
  static constexpr uint32_t READ_TIMEOUT_MS = 1000;
  static constexpr const char* PING_PATH = "/api/ping";
  static constexpr const char* EXPECTED_ANSWER = "SmsRelay";

  Config* config_ = nullptr;
  ConfigStore* store_ = nullptr;
  bool autoChecked_ = false;
  bool scanning_ = false;
  bool manualCheckFailed_ = false;
  IPAddress prefix_;
  uint16_t next_ = IP_START;

  void finish() {
    if (!scanning_) return;
    scanning_ = false;
    WiFi.setSleep(true);  // back to the default power saving mode
  }

  // Plain http (never https): the client used below is a non secure WiFiClient.
  static String pingUrl(const IPAddress& ip) { return String("http://") + ip.toString() + PING_PATH; }

  static bool probe(const String& url) {
    WiFiClient client;
    HTTPClient http;
    http.setConnectTimeout(CONNECT_TIMEOUT_MS);
    http.setTimeout(READ_TIMEOUT_MS);
    if (!http.begin(client, url)) {
      return false;
    }

    bool found = false;
    if (http.GET() == HTTP_CODE_OK) {
      String body = http.getString();
      body.trim();
      found = body == EXPECTED_ANSWER;
    }
    http.end();
    return found;
  }
};
