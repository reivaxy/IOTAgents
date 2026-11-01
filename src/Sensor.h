#pragma once

#include <Arduino.h>

// Placeholder sensor facade: returns constant values until real drivers are implemented.
class Sensor {
 public:
  void begin() {}

  bool isLightOn() const { return false; }
  float readTemperature() const { return 24.5f; }  // degrees Celsius
  bool isWaterLevelOk() const { return true; }
};
