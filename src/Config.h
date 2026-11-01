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
    prefs.end();
  }

 private:
  static constexpr const char* NAMESPACE = "aquamon";
};
