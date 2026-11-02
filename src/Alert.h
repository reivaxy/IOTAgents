#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>

#include "Config.h"

// Sends alerts to the SmsRelay module: POSTs {"module": <module name>, "text": <message>}
// as JSON on http://<SmsRelay IP>/api/alerte. The IP is read from the settings at each
// call. send() blocks for up to the connect + read timeouts.
class Alert {
 public:
  void begin(const Config& config) { config_ = &config; }

  // Returns true if SmsRelay answered with a 2xx status.
  bool send(const String& text) {
    if (config_->smsRelayIp.isEmpty()) {
      Serial.println("Alert not sent: SmsRelay IP unknown");
      return false;
    }
    if (WiFi.getMode() != WIFI_STA || WiFi.status() != WL_CONNECTED) {
      Serial.println("Alert not sent: not connected to the home network");
      return false;
    }

    JsonDocument doc;
    doc["module"] = config_->name;
    // The home page URL follows the message, so the recipient can open the module page.
    doc["text"] = text + "\nhttp://" + WiFi.localIP().toString() + "/";
    String payload;
    serializeJson(doc, payload);

    // Plain http (never https): the client used below is a non secure WiFiClient.
    const String url = String("http://") + config_->smsRelayIp + API_PATH;
    Serial.printf("Sending alert: POST %s %s\n", url.c_str(), payload.c_str());

    WiFiClient client;
    HTTPClient http;
    http.setConnectTimeout(CONNECT_TIMEOUT_MS);
    http.setTimeout(READ_TIMEOUT_MS);
    http.setReuse(false);  // one request per connection: "Connection: close", socket closed by end()
    if (!http.begin(client, url)) {
      Serial.println("Alert not sent: invalid URL");
      return false;
    }
    http.addHeader("Content-Type", "application/json");
    const int code = http.POST(payload);
    http.end();
    client.stop();  // explicit close, whatever the outcome (timeout, error or answer)

    const bool ok = code >= 200 && code < 300;
    Serial.printf("Alert %s (HTTP %d)\n", ok ? "sent" : "failed", code);
    return ok;
  }

 private:
  static constexpr uint32_t CONNECT_TIMEOUT_MS = 1000;
  // SmsRelay answers only once the SMS is handled, which takes much longer than a ping.
  static constexpr uint32_t READ_TIMEOUT_MS = 10000;
  static constexpr const char* API_PATH = "/api/alerte";

  const Config* config_ = nullptr;
};
