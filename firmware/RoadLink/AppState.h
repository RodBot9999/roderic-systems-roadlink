#pragma once

#include <Arduino.h>
#include "AppConfig.h"

enum class CanBitrate : uint8_t {
  K125,
  K250,
  K500,
  K1000
};

enum class CanOperatingMode : uint8_t {
  ListenOnly,
  Normal
};

struct AppSettings {
  CanBitrate canBitrate = CanBitrate::K500;
  CanOperatingMode canMode = CanOperatingMode::ListenOnly;

  bool serialCanStreaming = false;
  bool serialUiMirror = true;
  bool webUiEnabled = true;
  bool simEnabled = true;
  bool simAutoSend = false;
  bool simSendGps = true;
  bool simSendObd = true;

  uint16_t uiRefreshMs = AppConfig::UI_REFRESH_DEFAULT_MS;
  uint16_t obdPollMs = AppConfig::OBD_POLL_DEFAULT_MS;
  uint32_t simSendIntervalMs = AppConfig::SIM_SEND_INTERVAL_DEFAULT_MS;
  uint8_t simServerIp[4] = {0, 0, 0, 0};
  uint16_t simServerPort = 0;
  uint32_t simAccessKey = 0;
};

inline const char* canBitrateLabel(CanBitrate value) {
  switch (value) {
    case CanBitrate::K125:  return "125 kbps";
    case CanBitrate::K250:  return "250 kbps";
    case CanBitrate::K500:  return "500 kbps";
    case CanBitrate::K1000: return "1 Mbps";
  }
  return "Unknown";
}

inline const char* canModeLabel(CanOperatingMode value) {
  return value == CanOperatingMode::ListenOnly ? "Listen only" : "Normal / TX";
}
