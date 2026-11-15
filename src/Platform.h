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

void disableWifiSleep();

void setHttpConnectTimeout(WiFiClient& client, HTTPClient& http, uint32_t timeoutMs);

String firmwareUpdateError();

size_t firmwareUpdateSize();