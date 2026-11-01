#pragma once

#include <Arduino.h>
#include <Preferences.h>

namespace ConfigDefaults {
constexpr const char* MODULE_NAME = "NewAquaMonitor";
constexpr const char* AP_SSID = "NewAquaMonitor";
constexpr const char* AP_PWD = "password";
}  // namespace ConfigDefaults

struct Config {
  String name;
  String homeSsid;
  String homePwd;
  String apSsid;
  String apPwd;
  bool flipScreen = true;       // rotate the display by 180 degrees
  String smsRelayIp;            // empty = not discovered yet
  int32_t utcOffset = 0;        // seconds, valid only if utcOffsetReadAt > 0
  int64_t utcOffsetReadAt = 0;  // Unix epoch (UTC) when utcOffset was fetched; 0 = never
};

// Persists Config in NVS through Preferences.
class ConfigStore {
 public:
  Config load() {
    Config c;
    Preferences prefs;
    prefs.begin(NAMESPACE, true);
    c.name = prefs.getString("name", ConfigDefaults::MODULE_NAME);
    c.homeSsid = prefs.getString("homeSsid", "");
    c.homePwd = prefs.getString("homePwd", "");
    c.apSsid = prefs.getString("apSsid", "");
    c.apPwd = prefs.getString("apPwd", "");
    c.flipScreen = prefs.getBool("flip", true);
    c.smsRelayIp = prefs.getString("smsRelayIp", "");
    c.utcOffset = prefs.getInt("utcOffset", 0);
    c.utcOffsetReadAt = prefs.getLong64("utcOffsetAt", 0);
    prefs.end();
    return c;
  }

  void save(const Config& c) {
    Preferences prefs;
    prefs.begin(NAMESPACE, false);
    prefs.putString("name", c.name);
    prefs.putString("homeSsid", c.homeSsid);
    prefs.putString("homePwd", c.homePwd);
    prefs.putString("apSsid", c.apSsid);
    prefs.putString("apPwd", c.apPwd);
    prefs.putString("smsRelayIp", c.smsRelayIp);
    prefs.end();
  }

  void saveFlipScreen(const Config& c) {
    Preferences prefs;
    prefs.begin(NAMESPACE, false);
    prefs.putBool("flip", c.flipScreen);
    prefs.end();
  }

  void saveSmsRelayIp(const Config& c) {
    Preferences prefs;
    prefs.begin(NAMESPACE, false);
    prefs.putString("smsRelayIp", c.smsRelayIp);
    prefs.end();
  }

  void saveUtcOffset(const Config& c) {
    Preferences prefs;
    prefs.begin(NAMESPACE, false);
    prefs.putInt("utcOffset", c.utcOffset);
    prefs.putLong64("utcOffsetAt", c.utcOffsetReadAt);
    prefs.end();
  }

 private:
  static constexpr const char* NAMESPACE = "aquamon";
};
