#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <IPAddress.h>
#include <functional>

class Alert;
struct Config;
class ConfigStore;

#define IP_START 1
#define IP_END 255

// Finds the SmsRelay module on the home network (/24 subnet of this module) by calling
// /api/ping (POST, the same JSON payload as /api/status) on x.x.x.1 .. x.x.x.255
// until the answer identifies "SmsRelay", then stores the IP.
// Non blocking by steps: update() probes a single address per call, so it must be called
// from loop(). Once connected to the home network, a stored SmsRelay IP is checked with
// /api/ping; the scan starts automatically if none is stored or if the stored one does not
// answer. start() forces a new scan. Once the SmsRelay is known, it is pinged every
// PING_INTERVAL_MS; a failed ping makes isNotFound() true until a later ping succeeds.
class SmsRelayScanner {
 public:
  using StatusProvider = std::function<JsonDocument()>;

  static constexpr uint32_t SAVE_CHECK_TIMEOUT_MS = 10000;  // connect and read, check on config save
  static constexpr uint32_t PING_INTERVAL_MS = 10UL * 60 * 1000;

  void begin(Config& config, ConfigStore& store, Alert& alert, StatusProvider statusProvider);

  // Starts (or restarts) a scan. Returns false if not connected to the home network.
  bool start();

  bool isScanning() const;

  // True when no SmsRelay is known: either a scan ended without leaving any IP stored, or the
  // last manually saved IP did not answer, or the last periodic ping failed. Never true while
  // scanning.
  bool isNotFound() const;

  void stop();

  // A new network needs a fresh ping with the module's new STA address.
  void onWifiConnected(bool fallback);

  // Calls /api/ping on a manually entered IP and remembers whether SmsRelay answered (this drives
  // isNotFound()). An empty IP clears the failure. Nothing is checked (true is returned) when not
  // connected to the home network. Blocks for up to twice timeoutMs (connect + read).
  bool verify(const String& ip, uint32_t timeoutMs = SCAN_TIMEOUT_MS);

  static bool isStaConnected();

  void update();

 private:
  static constexpr uint32_t SCAN_TIMEOUT_MS = 2000;  // connect and read, scan and startup check
  static constexpr const char* PING_PATH = "/api/ping";
  static constexpr const char* FALLBACK_AP_IP = "192.168.4.1";
  static constexpr const char* EXPECTED_ANSWER = "SmsRelay";
  static constexpr const char* CONNECTED_MESSAGE = "module connected";

  Config* config_ = nullptr;
  ConfigStore* store_ = nullptr;
  Alert* alert_ = nullptr;
  StatusProvider statusProvider_;
  bool usingFallbackWifi_ = false;
  bool autoChecked_ = false;
  bool scanning_ = false;
  bool manualCheckFailed_ = false;
  bool pingFailed_ = false;
  uint32_t lastPing_ = 0;
  IPAddress prefix_;
  uint16_t next_ = IP_START;

  void finish();

  // Plain http (never https): the client used below is a non secure WiFiClient.
  static String pingUrl(const IPAddress& ip);

  // Diagnostic only: a live host answers a SYN within milliseconds (SYN-ACK, or RST when the port
  // is closed); "no answer" means the packets are lost on the way.
  static void logReachability(const IPAddress& ip, uint16_t port);

  bool probe(const String& url, uint32_t timeoutMs);
};
