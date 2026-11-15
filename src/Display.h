#pragma once

#include <Arduino.h>
#include <XIOTDisplay.h>

class TimeSync;

// SSD1306 OLED display: title (module name) and, on the last line, the IP address and the
// local time centered. Call begin() once from setup(), then update() and refresh() from loop().
class Display {
 public:
  // Created here rather than in a constructor: I2C must not be used before Arduino init.
  void begin(const String& title, bool flip);

  // Rotates the screen by 180 degrees (flip) or restores the normal orientation.
  void setFlip(bool flip);

  void setTitle(const String& title);

  // Updates the IP and time (last line) once per second. While smsRelayMissing is true,
  // lines 2 and 3 show a warning.
  void update(const TimeSync& timeSync, bool smsRelayMissing);

  // Replaces the normal screen by a QR code of `text` for durationMs. The QR is drawn once;
  // refresh() then leaves the screen untouched until the delay is over.
  // Returns false if the text does not fit in the QR code.
  bool showQr(const String& text, uint32_t durationMs);

  void refresh();

 private:
  static constexpr int OLED_ADDR = 0x3C;
#if defined(ESP8266)
  static constexpr int OLED_SDA = 4;
  static constexpr int OLED_SCL = 5;
#else
  static constexpr int OLED_SDA = 23;
  static constexpr int OLED_SCL = 18;
#endif
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
