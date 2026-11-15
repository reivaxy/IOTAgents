#pragma once

#include <Preferences.h>
#include "GenericModule.h"
#include "HttpServer.h"

// Quantities are dispenser units until the motor and food sensor are implemented.
class AquaFeederModule : public GenericModule {
 public:
  using GenericModule::GenericModule;
  static constexpr int REVERSE_QUANTITY = -100;
  static constexpr int MAX_QUANTITY = 10000;
  static constexpr size_t MAX_FEEDS = 4;

  const char* getType() const override { return "aquafeeder"; }
  void begin() override;
  void update() override;
  JsonDocument getStatus() override;
  String getHomeHtml() override;
  String getConfigHtml() override;
  bool saveConfig(JsonObjectConst cfg, String& error) override;

 protected:
  // Hardware integration points. The current mock finishes immediately, without GPIO writes.
  virtual bool runDispenser(int quantity) {
    Serial.printf("Mock feeder run: %d units\n", quantity);
    return true;
  }
  virtual bool dispensingDetected() { return mockSensorOk_; }

 private:
  struct Feed {
    int minute = -1;  // -1 disables this slot
    int quantity = 0;
  };
  static constexpr const char* NAMESPACE = "aquafeeder";
  Feed feeds_[MAX_FEEDS];
  int64_t lastScheduledMinute_ = -1;
  int64_t previousEpoch_ = 0;
  int previousQuantity_ = 0;
  bool previousSuccess_ = false;
  bool mockSensorOk_ = true;
  String alertMessage_;
  String storageError_;

  static bool parseTime(const char* text, int& minute);
  static String formatTime(int minute);
  static bool parseFeeds(JsonObjectConst cfg, Feed (&feeds)[MAX_FEEDS], String& error);
  static JsonDocument scheduleDocument(const Feed (&feeds)[MAX_FEEDS]);
  static bool writeJson(const char* key, const JsonDocument& doc);
  bool clockReady() const { return timeSync_.isSynced() && timeSync_.hasUtcOffset(); }
  void load();
  void handleRun();
  bool dispense(int quantity);
};