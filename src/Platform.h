#pragma once

#include <Arduino.h>

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <Updater.h>
using WebServer = ESP8266WebServer;
#else
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <Update.h>
#endif

inline void disableWifiSleep() {
#if defined(ESP8266)
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
#else
  WiFi.setSleep(false);
#endif
}

inline void setHttpConnectTimeout(WiFiClient& client, HTTPClient& http, uint32_t timeoutMs) {
#if defined(ESP8266)
  client.setTimeout(timeoutMs);
#else
  http.setConnectTimeout(timeoutMs);
#endif
}

inline String firmwareUpdateError() {
#if defined(ESP8266)
  return Update.getErrorString();
#else
  return Update.errorString();
#endif
}

inline size_t firmwareUpdateSize() {
#if defined(ESP8266)
  return (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
#else
  return UPDATE_SIZE_UNKNOWN;
#endif
}