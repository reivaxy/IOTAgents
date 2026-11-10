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
- each device type (for example, `feeder` and `aquamonitor`) gets its own branch and module class deriving from `GenericModule`
- only the device-specific behavior is overridden

The base contract is defined in [src/GenericModule.h](src/GenericModule.h).

## ESP32 and ESP8266 compatibility

The shared platform supports **ESP32 and ESP8266** with the same module contract, web pages, JSON API, WiFi fallback, time synchronization, alerts, and browser-based firmware updates. Check the available profiles in [platformio.ini](platformio.ini).

Both profiles use the Arduino framework, C++17, and a 115200-baud serial monitor. Set the upload and monitor ports to match your device. Select the corresponding environment in PlatformIO to build, upload, or monitor it.

[src/Platform.h](src/Platform.h) handles differences in WiFi headers, HTTP clients, web server types, WiFi sleep control, and firmware update APIs. Include it when module code needs those shared networking APIs rather than importing ESP32-only headers. ESP8266 uses an `ESP8266WebServer` alias named `WebServer`.

Settings use the `Preferences` API: native NVS on ESP32 and the `vshymanskyy/Preferences@^2.1.0` compatibility library on ESP8266. Device-specific settings should use their own namespace rather than the shared `aquamon` namespace used by `ConfigStore`; do not assume ESP32-native NVS APIs are portable.

The SSD1306 OLED is configured at I2C address `0x3C` in [src/Display.h](src/Display.h):

| Target | SDA | SCL |
| --- | --- | --- |
| ESP32 | GPIO23 | GPIO18 |
| ESP8266 | GPIO4 (D2 on NodeMCU-style boards) | GPIO5 (D1 on NodeMCU-style boards) |

GPIO numbers are authoritative; board labels may differ. Product-specific sensors, actuators, libraries, and pins still need to be checked for each target. ESP8266 also has less RAM and flash headroom, so keep module HTML/JSON payloads and buffers small and avoid long-running work in `update()`.

Firmware updates are available from **Generic Settings** and use `POST /api/update`. Upload only a binary built for the target board. ESP8266 OTA requires enough free sketch space for the new image; ESP32 requires an OTA-capable partition layout. [scripts/firmware_name.py](scripts/firmware_name.py) names the binary `firmware_<sanitized-branch>_<chip>.bin`, where `<chip>` is `esp32` or `esp8266`, so the target remains identifiable when the binary is copied out of its PlatformIO environment output directory.

## 1) What is common to all modules

The following parts are shared by every device:

- WiFi connection and access point setup
- settings saving and loading
- NTP / time sync
- alert system and SMS relay handling: check the [companion project SMSRelay](https://github.com/reivaxy/SMSRelay) used to send SMS and Push notifications. Manual setting or auto discovery.
- display management
- HTTP server wiring
- generic pages and generic REST endpoints
- browser-based OTA firmware updates and device restart
- the common module entry point created in [src/main.cpp](src/main.cpp)

This is the part that should remain stable across branches. In other words, if a change is needed for the platform itself, it needs to be done once in the shared code (`main` branch) and then reused by every device by merging "main" into the device own branch.

## 2) What is specific to each module

Each module is different in behavior and UI. Typical per-module logic includes:

- the device type name, such as `feeder` or `aquamonitor`
- custom status data returned by `getStatus()` under the `module` object in `/api/status`
- custom rows / script injected on the home status page via `getStatusHtml()`
- custom config page content via `getConfigHtml()`
- validation and persistence of device-specific settings in `saveConfig()`
- custom `begin()` and `update()` logic for sensor polling, actuator control, timers, etc.

Override only the methods needed by the derived module. `getConfigHtml()` is inserted into the dedicated `/module/config` page; its own script must send JSON to `POST /api/module/config`. The generic page does not submit module forms automatically.

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

The default `begin()` and `update()` do nothing. The status and HTML methods return placeholders; `saveConfig()` rejects settings with `false` and the error "This module has no specific settings". This makes `GenericModule` usable as a generic fallback while allowing real modules to override only the behaviors they need.

The constructor receives `Display`, `Config`, `Alert`, `HttpServer`, and `TimeSync` references, available to subclasses as `display_`, `config_`, `alert_`, `httpServer_`, and `timeSync_`. The base class also has a virtual destructor.

`begin()` runs once in `setup()`, after the shared services are initialized but before WiFi connection and HTTP server startup. Extra routes can be registered through `httpServer_.webServer()` there; include [src/HttpServer.h](src/HttpServer.h) in the implementation when using it. `update()` runs on every `loop()` iteration and should return promptly so shared services remain responsive.

`saveConfig()` must validate, apply, and persist module settings itself. Return `true` for HTTP 200 with `{"ok":true}`, or return `false` and set `error` for HTTP 400 with `{"error":"..."}`. Loading persisted module settings is also the subclass's responsibility, typically in `begin()`.

## 4) How to create a new module branch

A good pattern is to keep the shared project on the main branch and create one branch per hardware profile.

For example, create a feeder branch:

```bash
git checkout main
git pull

git checkout -b feeder
```

For an aquamonitor, start again from `main` and create `aquamonitor` instead. These are alternative product branches, not branches created from one another.

Create fix or feature branches from the relevant product branch, for example:

```bash
git checkout feeder
git checkout -b feeder/fix/something
```

Likewise, start from `feeder` again before creating `feeder/feature/somethingElse`. Slashes are a naming convention, not a Git parent/child relationship.

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
    // Replace with validation, application, and persistence of feeder settings.
    (void)cfg;
    error = "Feeder settings are not implemented in this example";
    return false;
  }
};
```

Then in [src/main.cpp](src/main.cpp), include the device-specific class header and replace the generic module instance with the device-specific one:

```cpp
FeederModule module(display, config, alert, httpServer, timeSync);
```

## 6) Example: an aquamonitor module

The aquamonitor is similar in architecture, but its status and settings are focused on water monitoring. The following is an illustrative status and HTML fragment, not a complete settings implementation:

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
      <form onsubmit="event.preventDefault()">
        <label>Low water threshold <input name="lowLevel" value="20"></label>
      </form>
    )";
  }
};
```

To make this form functional, render the stored threshold instead of a hard-coded value, add a submit handler posting JSON to `/api/module/config`, and override `saveConfig()` to validate, apply, and persist it. Without that override, the base implementation rejects module settings.

And in [src/main.cpp](src/main.cpp), after including the device-specific class header:

```cpp
AquaMonitorModule module(display, config, alert, httpServer, timeSync);
```

## 7) Common vs. specific summary

Common for every device:

- `Display`, `Config`, `Alert`, `TimeSync`, `HttpServer`, `SmsRelayScanner`
- base lifecycle (`begin()`, `update()`)
- generic status and config infrastructure
- shared WiFi / HTTP / networking behavior
- ESP32 / ESP8266 compatibility through [src/Platform.h](src/Platform.h)
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
