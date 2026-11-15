#pragma once

#include <Arduino.h>

namespace ConfigDefaults {
constexpr const char* MODULE_NAME = "IOTAgent_Generic";
constexpr const char* AP_SSID = "IOTAgent";
constexpr const char* AP_PWD = "password";
}  // namespace ConfigDefaults

struct Config {
  String name;
  String homeSsid;
  String homePwd;
  String apSsid;
  String apPwd;
  String fallbackSsid;          // fallback WiFi SSID from SmsRelay
  String fallbackPwd;           // fallback WiFi password from SmsRelay
  bool flipScreen = true;       // rotate the display by 180 degrees
  String smsRelayIp;            // empty = not discovered yet
  int32_t utcOffset = 0;        // seconds, valid only if utcOffsetReadAt > 0
  int64_t utcOffsetReadAt = 0;  // Unix epoch (UTC) when utcOffset was fetched; 0 = never
};

// Persists Config in NVS through Preferences.
class ConfigStore {
 public:
  Config load();

  void save(const Config& c);

  void saveFlipScreen(const Config& c);

  void saveSmsRelayIp(const Config& c);

  void saveFallbackWifi(const Config& c);

  void saveUtcOffset(const Config& c);

 private:
  static constexpr const char* NAMESPACE = "generic";

};
