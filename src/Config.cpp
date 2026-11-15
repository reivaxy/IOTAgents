#include "Config.h"

#include <Preferences.h>

Config ConfigStore::load() {
  Config c;
  Preferences prefs;
  prefs.begin(NAMESPACE, true);
  c.name = prefs.getString("name", ConfigDefaults::MODULE_NAME);
  c.homeSsid = prefs.getString("homeSsid", "");
  c.homePwd = prefs.getString("homePwd", "");
  c.apSsid = prefs.getString("apSsid", "");
  c.apPwd = prefs.getString("apPwd", "");
  c.fallbackSsid = prefs.getString("fallbackSsid", "");
  c.fallbackPwd = prefs.getString("fallbackPwd", "");
  c.flipScreen = prefs.getBool("flip", true);
  c.smsRelayIp = prefs.getString("smsRelayIp", "");
  c.utcOffset = prefs.getInt("utcOffset", 0);
  c.utcOffsetReadAt = prefs.getLong64("utcOffsetAt", 0);
  prefs.end();
  return c;
}

void ConfigStore::save(const Config& c) {
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

void ConfigStore::saveFlipScreen(const Config& c) {
  Preferences prefs;
  prefs.begin(NAMESPACE, false);
  prefs.putBool("flip", c.flipScreen);
  prefs.end();
}

void ConfigStore::saveSmsRelayIp(const Config& c) {
  Preferences prefs;
  prefs.begin(NAMESPACE, false);
  prefs.putString("smsRelayIp", c.smsRelayIp);
  prefs.end();
}

void ConfigStore::saveFallbackWifi(const Config& c) {
  Preferences prefs;
  prefs.begin(NAMESPACE, false);
  prefs.putString("fallbackSsid", c.fallbackSsid);
  prefs.putString("fallbackPwd", c.fallbackPwd);
  prefs.end();
}

void ConfigStore::saveUtcOffset(const Config& c) {
  Preferences prefs;
  prefs.begin(NAMESPACE, false);
  prefs.putInt("utcOffset", c.utcOffset);
  prefs.putLong64("utcOffsetAt", c.utcOffsetReadAt);
  prefs.end();
}