#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <XIOTDisplay.h>
#include <qrcode.h>
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

  // Updates the IP and time (last line) once per second. While smsRelayMissing is true,
  // lines 2 and 3 show a warning.
  void update(const TimeSync& timeSync, bool smsRelayMissing) {
    if (display_ == nullptr) return;
    if (lastUpdate_ != 0 && millis() - lastUpdate_ < UPDATE_MS) return;
    lastUpdate_ = millis();
    if (lastUpdate_ == 0) lastUpdate_ = 1;

    if (smsRelayMissing != warningShown_) {
      warningShown_ = smsRelayMissing;
      display_->setLineAlignment(LINE_WARNING_TITLE, TEXT_ALIGN_CENTER);
      display_->setLine(LINE_WARNING_TITLE, "WARNING", false, true);
      display_->setLineAlignment(LINE_WARNING_TEXT, TEXT_ALIGN_CENTER);
      display_->setLine(LINE_WARNING_TEXT, "SmsRelay not found" );
    }
    if (!smsRelayMissing) {
      display_->setLine(LINE_WARNING_TITLE, "", false, false);
      display_->setLine(LINE_WARNING_TEXT, "");
    }

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

  // Replaces the normal screen by a QR code of `text` for durationMs. The QR is drawn once;
  // refresh() then leaves the screen untouched until the delay is over.
  // Returns false if the text does not fit in the QR code.
  bool showQr(const String& text, uint32_t durationMs) {
    if (display_ == nullptr) return false;
    QRCode qrcode;
    uint8_t qrData[qrcode_getBufferSize(QR_VERSION)];
    if (qrcode_initText(&qrcode, qrData, QR_VERSION, ECC_LOW, text.c_str()) != 0) {
      Serial.println("QR code: text too long");
      return false;
    }

    // Dark modules are drawn unlit on a lit background (including the quiet zone), which is
    // the polarity phone scanners expect.
    SSD1306* oled = display_->getDisplay();
    const int size = (qrcode.size + 2 * QR_QUIET_ZONE) * QR_SCALE;
    const int x0 = (SCREEN_WIDTH - size) / 2;
    const int y0 = (SCREEN_HEIGHT - size) / 2;
    oled->clear();
    oled->setColor(WHITE);
    oled->fillRect(x0, y0, size, size);
    oled->setColor(BLACK);
    for (uint8_t y = 0; y < qrcode.size; y++) {
      for (uint8_t x = 0; x < qrcode.size; x++) {
        if (qrcode_getModule(&qrcode, x, y)) {
          oled->fillRect(x0 + (x + QR_QUIET_ZONE) * QR_SCALE, y0 + (y + QR_QUIET_ZONE) * QR_SCALE,
                         QR_SCALE, QR_SCALE);
        }
      }
    }
    oled->setColor(WHITE);
    oled->display();

    qrUntil_ = millis() + durationMs;
    if (qrUntil_ == 0) qrUntil_ = 1;
    return true;
  }

  void refresh() {
    if (display_ == nullptr) return;
    if (qrUntil_ != 0) {
      if (static_cast<int32_t>(millis() - qrUntil_) < 0) return;
      qrUntil_ = 0;
    }
    display_->refresh();
  }

 private:
  static constexpr int OLED_ADDR = 0x3C;
  static constexpr int OLED_SDA = 23;
  static constexpr int OLED_SCL = 18;
  static constexpr int LINE_WARNING_TITLE = 1;
  static constexpr int LINE_WARNING_TEXT = 2;
  static constexpr int LINE_STATUS = 4;  // last line: "<ip>   <time>", centered
  static constexpr uint32_t UPDATE_MS = 1000;
  static constexpr int SCREEN_WIDTH = 128;
  static constexpr int SCREEN_HEIGHT = 64;
  // Version 2 (25x25 modules, low error correction) holds up to 32 bytes, enough for an
  // http://<ip>/ URL. 2 px per module + 2 modules of quiet zone = 58 px, fits the 64 px height.
  static constexpr uint8_t QR_VERSION = 2;
  static constexpr int QR_SCALE = 2;
  static constexpr int QR_QUIET_ZONE = 2;

  DisplayClass* display_ = nullptr;
  uint32_t lastUpdate_ = 0;
  uint32_t qrUntil_ = 0;  // 0 = QR page not shown
  bool warningShown_ = false;
};
