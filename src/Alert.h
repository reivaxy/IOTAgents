#pragma once

#include <Arduino.h>

struct Config;

// Sends alerts to the SmsRelay module: POSTs {"module": <module name>, "text": <message>}
// as JSON on http://<SmsRelay IP>/api/alert. The IP is read from the settings at each
// call. send() blocks for up to the connect + read timeouts.
class Alert {
 public:
  void begin(const Config& config);

  // Returns true if SmsRelay answered with a 2xx status.
  bool send(const String& text);

 private:
  static constexpr uint32_t CONNECT_TIMEOUT_MS = 1000;
  // SmsRelay answers only once the SMS is handled, which takes much longer than a ping.
  static constexpr uint32_t READ_TIMEOUT_MS = 10000;
  static constexpr const char* API_PATH = "/api/alert";

  const Config* config_ = nullptr;
};
