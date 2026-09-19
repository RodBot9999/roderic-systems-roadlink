#pragma once

#include <Preferences.h>
#include "AppState.h"

class SettingsStore {
public:
  bool begin();
  void load(AppSettings& settings);
  void saveSim(const AppSettings& settings);

private:
  Preferences preferences_;
  bool ready_ = false;
};
