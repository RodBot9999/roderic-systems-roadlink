#pragma once

#include <Arduino.h>
#include "AppState.h"
#include "CanService.h"
#include "ObdService.h"
#include "A7670Service.h"
#include "SettingsStore.h"

class StreamingController {
public:
  StreamingController(
      CanService& can,
      ObdService& obd,
      A7670Service& modem,
      AppSettings& settings,
      SettingsStore& settingsStore);

  void begin();
  void update();

  bool start();
  void stop();
  bool running() const;

  void setGpsEnabled(bool enabled);
  void setObdEnabled(bool enabled);
  void toggleObdField(uint16_t field);
  void setIntervalMs(uint32_t intervalMs);
  void configurationChanged();

  uint8_t selectedObdFieldCount() const;
  const String& statusMessage() const;

private:
  bool startInternal(bool allowRetry);
  bool ensureObdTransmitMode();
  void synchronizeServices();
  void commitLocalChange();
  void applyRemoteConfig(const RemoteStreamingConfig& remote);

  CanService& can_;
  ObdService& obd_;
  A7670Service& modem_;
  AppSettings& settings_;
  SettingsStore& settingsStore_;

  bool ownsObdPolling_ = false;
  bool pendingRemoteStart_ = false;
  uint32_t lastStartRetryMs_ = 0;
  String statusMessage_ = "Streaming stopped";
};
