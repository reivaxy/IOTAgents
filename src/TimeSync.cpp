#include "TimeSync.h"

#include <ArduinoJson.h>
#include "Platform.h"
#include <WiFiClientSecure.h>

#include "Config.h"

void TimeSync::begin(Config& config, ConfigStore& store) {
  config_ = &config;
  store_ = &store;
}

void TimeSync::update() {
  if (WiFi.getMode() != WIFI_STA || WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (!started_) {
    applyNtp();
    started_ = true;
    Serial.println("NTP synchronisation started");
  }

  // The age of the stored offset can only be judged once the clock is valid.
  if (isSynced() && static_cast<int32_t>(millis() - nextOffsetCheckAt_) >= 0) {
    refreshOffset();
  }
}

bool TimeSync::isSynced() const { return started_ && time(nullptr) > MIN_VALID_EPOCH; }

time_t TimeSync::now() const { return time(nullptr); }

bool TimeSync::hasUtcOffset() const { return offsetKnown_; }

int32_t TimeSync::utcOffset() const { return utcOffset_; }

void TimeSync::setOffset(int32_t offset) {
  if (!offsetKnown_ || offset != utcOffset_) {
    utcOffset_ = offset;
    offsetKnown_ = true;
    applyNtp();
  }
}

void TimeSync::refreshOffset() {
  const int64_t now = time(nullptr);
  const int64_t age = now - config_->utcOffsetReadAt;

  if (config_->utcOffsetReadAt > 0 && age >= 0 && age < OFFSET_MAX_AGE_S) {
    setOffset(config_->utcOffset);
    nextOffsetCheckAt_ = millis() + static_cast<uint32_t>((OFFSET_MAX_AGE_S - age) * 1000);
    Serial.printf("Using stored UTC offset: %ld s (age %lld s)\n",
                  static_cast<long>(utcOffset_), age);
    return;
  }

  int32_t offset;
  if (fetchUtcOffset(offset)) {
    config_->utcOffset = offset;
    config_->utcOffsetReadAt = now;
    store_->saveUtcOffset(*config_);
    setOffset(offset);
    nextOffsetCheckAt_ = millis() + static_cast<uint32_t>(OFFSET_MAX_AGE_S * 1000);
    Serial.printf("Fetched UTC offset: %ld s\n", static_cast<long>(offset));
  } else {
    nextOffsetCheckAt_ = millis() + OFFSET_RETRY_MS;
    Serial.println("UTC offset fetch failed");
  }
}

void TimeSync::applyNtp() { configTime(utcOffset_, 0, "pool.ntp.org", "time.google.com"); }

bool TimeSync::parseOffset(const char* s, int32_t& out) {
  if (s == nullptr || strlen(s) != 5 || (s[0] != '+' && s[0] != '-')) return false;
  for (int i = 1; i < 5; i++) {
    if (!isdigit(static_cast<unsigned char>(s[i]))) return false;
  }
  const int hours = (s[1] - '0') * 10 + (s[2] - '0');
  const int minutes = (s[3] - '0') * 10 + (s[4] - '0');
  if (hours > 14 || minutes > 59) return false;
  const int32_t seconds = hours * 3600 + minutes * 60;
  out = (s[0] == '-') ? -seconds : seconds;
  return true;
}

bool TimeSync::fetchUtcOffset(int32_t& out) {
  WiFiClientSecure client;
  client.setInsecure();  // Certificate checks are deliberately disabled.

  HTTPClient http;
  setHttpConnectTimeout(client, http, HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setUserAgent("IOTAgent/1.0");
  http.setReuse(false);  // one request per connection: "Connection: close", socket closed by end()
  if (!http.begin(client, OFFSET_URL)) {
    return false;
  }

  bool ok = false;
  if (http.GET() == HTTP_CODE_OK) {
    JsonDocument filter;
    filter["utc_offset"] = true;
    JsonDocument doc;
    if (!deserializeJson(doc, http.getString(), DeserializationOption::Filter(filter))) {
      ok = parseOffset(doc["utc_offset"] | static_cast<const char*>(nullptr), out);
    }
  }
  http.end();
  client.stop();  // explicit close, whatever the outcome (timeout, error or answer)
  return ok;
}