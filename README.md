# Aquamonitor

This device is intended to monitor a few critical parameters of a reef tank or other kinds of aquarium:

- water temperature
- water level 
- lighting schedule.
- (wip, hopefully) water circulation

It relies on the IOTAGents module base class and features (see next chapter) which comes from this repository "main" branch.

## Components:

- ESP32-WROOM-32E
- DS18B20 waterproof temperature probe
- 4.7k resistor (pull-up between the DS18B20 DATA line and 3.3V)
- Water level switch (float switch)
- Photoresistor (LDR) and 2 fixed resistor aournd 22K to build the voltage divider
- SSD1306 I2C OLED display (0x3C)
- A companion SMSrelay device (see [SMSRelay](https://github.com/reivaxy/SMSRelay)) to transmit alerts over SMS and Push notif.


## Wiring:

| ESP32 pin | Connected to | Notes |
|-----------|--------------|-------|
| GPIO 4    | DS18B20 temperature probe (DATA) | OneWire bus. Needs a 4.7k pull-up resistor between DATA and 3.3V. |
| GPIO 25   | Water level switch | Digital input with internal pull-up: the switch connects the pin to GND. Pin HIGH = water level OK. |
| GPIO 34   | Photoresistor (light sensor) | Analog input on ADC1 (ADC2 is unusable while WiFi is on). 12-bit reading, light considered ON when the raw value is 1000 or more. Input-only pin: it has no internal pull-up, so use an external voltage divider. |
| GPIO 23   | SSD1306 OLED display (SDA) | I2C, address 0x3C. |
| GPIO 18   | SSD1306 OLED display (SCL) | I2C, address 0x3C. |
| 3.3V      | DS18B20 VDD, OLED VCC, sensors supply | |
| GND       | Common ground of all components | |

The pin assignments are defined in `src/AquaMonitor.h` (sensors) and `src/Display.h` (OLED).


# IOTAgents module architecture

This project is built around a shared base class, `GenericModule`, and a set of device-specific implementations. The idea is:

- common infrastructure stays in the `main` branch
- each device type (for example, `feeder` and `aquamonitor`) gets its own branch and module class overriding the GenericModule class.
- only the device-specific behavior is overridden

The base contract is defined in `src/GenericModule.h`.

## 1) What is common to all modules

The following parts are shared by every device:

- WiFi connection and access point setup
- settings saving and loading
- NTP / time sync
- alert system and SMS relay handling: check the [companion project SMSRelay](https://github.com/reivaxy/SMSRelay) used to send SMS and Push notifications. Manual setting or auto discovery.
- display management
- HTTP server wiring
- generic pages and generic REST endpoints
- the common module entry point created in `src/main.cpp`

This is the part that should remain stable across branches. In other words, if a change is needed for the platform itself, it needs to be done once in the shared code (`main` branch) and then reused by every device by merging "main" into the device own branch.

## 2) What is specific to each module

Each module is different in behavior and UI. Typical per-module logic includes:

- the device type name, such as `feeder` or `aquamonitor`
- custom status data returned by `getStatus()` under the `module` object in `/api/status`
- custom rows / script injected on the home status page via `getStatusHtml()`
- custom config page content via `getConfigHtml()`
- validation and persistence of device-specific settings in `saveConfig()`
- custom `begin()` and `update()` logic for sensor polling, actuator control, timers, etc.

Those are the methods that must be overridden in a derived module.

## 3) The base contract

`GenericModule` exposes the following extension points:

```cpp
class GenericModule {
 public:
  virtual const char* getType() const { return "generic"; }
  virtual void begin() {}
  virtual void update() {}
  virtual JsonDocument getStatus() { ... }
  virtual String getStatusHtml() { ... }
  virtual String getConfigHtml() { ... }
  virtual bool saveConfig(JsonObjectConst cfg, String& error) { ... }
};
```

The default implementations do nothing and return a placeholder message. This makes `GenericModule` usable as a generic fallback while allowing real modules to override only the behaviors they need.

## 4) How to create a new module branch

A good pattern is to keep the shared project on the main branch and create one branch per hardware profile.

Example branch names:

```bash
git checkout main
git pull

git checkout -b feeder
git checkout -b aquamonitor
```

Specific module branches will be created under the module branch:

```bash
git checkout -b feeder/fix/something
git checkout -b feeder/feature/somethingElse
```

The branch should contain the module-specific class and any settings that differ for that product, while the rest of the codebase remains shared.

## 5) Example: a feeder module

A feeder module normally derives from `GenericModule` and overrides only the behavior that differs from the generic platform.

```cpp
class FeederModule : public GenericModule {
 public:
  FeederModule(Display& display, Config& config, Alert& alert, HttpServer& httpServer,
               TimeSync& timeSync)
      : GenericModule(display, config, alert, httpServer, timeSync) {}

  const char* getType() const override { return "feeder"; }

  void begin() override {
    // feeder-specific initialization
  }

  void update() override {
    // feed schedule, stepper motor control, etc.
  }

  JsonDocument getStatus() override {
    JsonDocument doc;
    doc["feedingEnabled"] = true;
    doc["nextFeed"] = "08:00";
    return doc;
  }

  String getStatusHtml() override {
    return R"(
      <tr><td>Feeder</td><td id="feederStatus">-</td></tr>
      <script>
        function moduleStatus(module) {
          document.getElementById("feederStatus").innerText = module.feedingEnabled ? "ON" : "OFF";
        }
      </script>
    )";
  }

  bool saveConfig(JsonObjectConst cfg, String& error) override {
    // validate feeder config
    return true;
  }
};
```

Then in `src/main.cpp`, replace the generic module instance with the device-specific one:

```cpp
FeederModule module(display, config, alert, httpServer, timeSync);
```

## 6) Example: an aquamonitor module

The aquamonitor is similar in architecture, but its status and settings are focused on water monitoring.

```cpp
class AquaMonitorModule : public GenericModule {
 public:
  AquaMonitorModule(Display& display, Config& config, Alert& alert, HttpServer& httpServer,
                    TimeSync& timeSync)
      : GenericModule(display, config, alert, httpServer, timeSync) {}

  const char* getType() const override { return "aquamonitor"; }

  JsonDocument getStatus() override {
    JsonDocument doc;
    doc["temperature"] = 24.5;
    doc["ph"] = 7.2;
    doc["waterLevel"] = 82;
    return doc;
  }

  String getConfigHtml() override {
    return R"(
      <h2>AquaMonitor settings</h2>
      <form>
        <label>Low water threshold <input name="lowLevel" value="20"></label>
      </form>
    )";
  }
};
```

And in `main.cpp`:

```cpp
AquaMonitorModule module(display, config, alert, httpServer, timeSync);
```

## 7) Common vs. specific summary

Common for every device:

- `Display`, `Config`, `Alert`, `TimeSync`, `HttpServer`, `SmsRelayScanner`
- base lifecycle (`begin()`, `update()`)
- generic status and config infrastructure
- shared WiFi / HTTP / networking behavior
- platform-wide defaults and project setup

Specific for each device:

- `getType()` return value
- custom `getStatus()` payload
- custom `getStatusHtml()` markup
- custom `getConfigHtml()` form
- custom `saveConfig()` handling
- device-specific scheduling, sensor processing, logic, alarms, and actuators

## 8) Recommended team workflow

A simple and clean approach is:

1. keep the shared project stable on `main`
2. create one module branch per product
3. override only the required `GenericModule` methods in that branch
4. keep the common code in the shared files
5. merge common fixes back into `main` and rebase or merge module branches as needed

This keeps each product branch focused and avoids mixing feeder logic with aquamonitor logic.

## 9) Practical rule

If the functionality is shared by all devices, put it in the common project code (`main` branch)

If the functionality belongs only to one product, override it in the module subclass for that product branch.

That is the core of this architecture.
