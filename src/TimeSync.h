#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

// Starts SNTP once the STA connection is available. Call update() from loop().
// The module time is UTC.
class TimeSync {
 public:
  void update() {
    if (!started_ && WiFi.getMode() == WIFI_STA && WiFi.status() == WL_CONNECTED) {
      configTime(0, 0, "pool.ntp.org", "time.google.com");
      started_ = true;
      Serial.println("NTP synchronisation started");
    }
  }

  bool isSynced() const { return started_ && time(nullptr) > MIN_VALID_EPOCH; }

  // Seconds since the Unix epoch (UTC); only meaningful when isSynced().
  time_t now() const { return time(nullptr); }

 private:
  static constexpr time_t MIN_VALID_EPOCH = 1700000000;  // Nov 2023
  bool started_ = false;
};
