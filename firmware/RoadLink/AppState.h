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

namespace StreamField {
constexpr uint16_t RPM         = 1U << 0;
constexpr uint16_t SPEED       = 1U << 1;
constexpr uint16_t COOLANT     = 1U << 2;
constexpr uint16_t THROTTLE    = 1U << 3;
constexpr uint16_t MANIFOLD    = 1U << 4;
constexpr uint16_t INTAKE_TEMP = 1U << 5;
constexpr uint16_t TIMING      = 1U << 6;
constexpr uint16_t VOLTAGE     = 1U << 7;
constexpr uint16_t FUEL_LEVEL  = 1U << 8;
constexpr uint16_t ALL = RPM | SPEED | COOLANT | THROTTLE | MANIFOLD |
    INTAKE_TEMP | TIMING | VOLTAGE | FUEL_LEVEL;
constexpr uint16_t DEFAULT_MASK = RPM | SPEED | COOLANT | VOLTAGE;
}

struct AppSettings {
  CanBitrate canBitrate = CanBitrate::K500;
  CanOperatingMode canMode = CanOperatingMode::ListenOnly;

  bool simEnabled = true;
  bool simSendGps = true;
  bool simSendObd = true;

  uint16_t uiRefreshMs = AppConfig::UI_REFRESH_DEFAULT_MS;
  uint16_t obdPollMs = AppConfig::OBD_POLL_DEFAULT_MS;
  uint32_t simSendIntervalMs = AppConfig::SIM_SEND_INTERVAL_DEFAULT_MS;
  uint16_t simObdFieldMask = StreamField::DEFAULT_MASK;
  uint32_t streamConfigRevision = 1;
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
