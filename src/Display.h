#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <XIOTDisplay.h>
#include <time.h>

#include "TimeSync.h"

// SSD1306 OLED display: title (module name) and, on the last line, the IP address and the
// local time centered. Call begin() once from setup(), then update() and refresh() from loop().
class Display {
 public:
  // Created here rather than in a constructor: I2C must not be used before Arduino init.
  void begin(const String& title, bool flip) {
    display_ = new DisplayClass(OLED_ADDR, OLED_SDA, OLED_SCL, flip);
    display_->setLineAlignment(LINE_STATUS, TEXT_ALIGN_CENTER);
    display_->setTitle(title.c_str());
    display_->refresh();
  }

  // Rotates the screen by 180 degrees (flip) or restores the normal orientation.
  void setFlip(bool flip) {
    if (display_ == nullptr) return;
    if (flip) {
      display_->getDisplay()->flipScreenVertically();
    } else {
      display_->getDisplay()->resetOrientation();
    }
  }

  void setTitle(const String& title) {
    if (display_ != nullptr) display_->setTitle(title.c_str());
  }

  // Updates the IP and time (last line) once per second.
  void update(const TimeSync& timeSync) {
    if (display_ == nullptr) return;
    if (lastUpdate_ != 0 && millis() - lastUpdate_ < UPDATE_MS) return;
    lastUpdate_ = millis();
    if (lastUpdate_ == 0) lastUpdate_ = 1;

    String ip;
    const wifi_mode_t mode = WiFi.getMode();
    const bool connected = mode == WIFI_STA && WiFi.status() == WL_CONNECTED;
    if (connected) {
      ip = WiFi.localIP().toString();
    } else if (mode == WIFI_AP) {
      ip = WiFi.softAPIP().toString();
    }
    // Top right icon: steady when connected to the home network, blinking otherwise.
    display_->wifiIcon(!connected, mode == WIFI_AP ? AP : STA);
    String status = ip;

    if (timeSync.isSynced()) {
      // configTime() already applies the UTC offset, so localtime_r gives local time.
      const time_t now = timeSync.now();
      struct tm tmLocal;
      localtime_r(&now, &tmLocal);
      char timeText[9];
      strftime(timeText, sizeof(timeText), "%H:%M:%S", &tmLocal);
      if (!status.isEmpty()) status += "   ";
      status += timeText;
    }
    display_->setLine(LINE_STATUS, status.c_str());
  }

  void refresh() {
    if (display_ != nullptr) display_->refresh();
  }

 private:
  static constexpr int OLED_ADDR = 0x3C;
  static constexpr int OLED_SDA = 23;
  static constexpr int OLED_SCL = 18;
  static constexpr int LINE_STATUS = 4;  // last line: "<ip>   <time>", centered
  static constexpr uint32_t UPDATE_MS = 1000;

  DisplayClass* display_ = nullptr;
  uint32_t lastUpdate_ = 0;
};
