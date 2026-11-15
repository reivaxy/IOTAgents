#include "Platform.h"

void disableWifiSleep() {
#if defined(ESP8266)
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
#else
  WiFi.setSleep(false);
#endif
}

void setHttpConnectTimeout(WiFiClient& client, HTTPClient& http, uint32_t timeoutMs) {
#if defined(ESP8266)
  client.setTimeout(timeoutMs);
#else
  http.setConnectTimeout(timeoutMs);
#endif
}

String firmwareUpdateError() {
#if defined(ESP8266)
  return Update.getErrorString();
#else
  return Update.errorString();
#endif
}

size_t firmwareUpdateSize() {
#if defined(ESP8266)
  return (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
#else
  return UPDATE_SIZE_UNKNOWN;
#endif
}