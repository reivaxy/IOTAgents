#pragma once

#include <Arduino.h>
#include <time.h>

struct Config;
class ConfigStore;

// Starts SNTP once the STA connection is available and determines the UTC offset:
// from the stored config if it was read less than 6 hours ago, otherwise from ipapi.co
// (then stored). Call begin() once, then update() from loop().
class TimeSync {
 public:
  void begin(Config& config, ConfigStore& store);

  void update();

  bool isSynced() const;

  // Seconds since the Unix epoch (UTC); only meaningful when isSynced().
  time_t now() const;

  bool hasUtcOffset() const;

  // Offset from UTC in seconds; 0 until hasUtcOffset().
  int32_t utcOffset() const;

 private:
  static constexpr time_t MIN_VALID_EPOCH = 1700000000;  // Nov 2023
  static constexpr uint32_t OFFSET_RETRY_MS = 60UL * 1000;
  static constexpr int64_t OFFSET_MAX_AGE_S = 6LL * 60 * 60;
  static constexpr uint32_t HTTP_TIMEOUT_MS = 5000;
  static constexpr const char* OFFSET_URL = "https://ipapi.co/json/";

  bool started_ = false;
  bool offsetKnown_ = false;
  int32_t utcOffset_ = 0;
  uint32_t nextOffsetCheckAt_ = 0;
  Config* config_ = nullptr;
  ConfigStore* store_ = nullptr;

  void setOffset(int32_t offset);

  void refreshOffset();

  void applyNtp();

  // Parses "+0100" / "-0530" into seconds.
  static bool parseOffset(const char* s, int32_t& out);

  bool fetchUtcOffset(int32_t& out);
};
