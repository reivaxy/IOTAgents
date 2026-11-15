#include "Alert.h"

#include <ArduinoJson.h>
#include "Platform.h"
#include <WiFiClient.h>

#include "Config.h"

void Alert::begin(const Config& config) { config_ = &config; }

bool Alert::send(const String& text) {
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
  setHttpConnectTimeout(client, http, CONNECT_TIMEOUT_MS);
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