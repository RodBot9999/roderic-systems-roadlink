#pragma once

#include <Arduino.h>
#include <HTTPUpdateServer.h>
#include <WebServer.h>

// Local Wi-Fi transport only. The ESP32 Update library writes the inactive
// application slot; a future cellular downloader can use that same slot.
class LocalFirmwareUpdateService {
public:
  void begin();
  void update();
  void setEnabled(bool enabled);
  bool active() const;

private:
  String loadOrCreatePassword();
  void stop();

  WebServer server_{80};
  HTTPUpdateServer updater_;
  bool active_ = false;
  bool requestedEnabled_ = false;
  bool routesConfigured_ = false;
};
