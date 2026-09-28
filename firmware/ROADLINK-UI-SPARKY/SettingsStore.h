#pragma once

#include <Preferences.h>
#include "AppState.h"

class SettingsStore {
public:
  bool begin();
  void load(AppSettings& settings);
  void saveSim(const AppSettings& settings);
  bool saveHeartbeatEnabled(bool enabled);
  bool saveOtaEnabled(bool enabled);
  bool saveUiTheme(uint8_t theme);

private:
  Preferences preferences_;
  bool ready_ = false;
};
