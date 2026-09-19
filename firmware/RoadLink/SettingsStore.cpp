#include "SettingsStore.h"

bool SettingsStore::begin() {
  ready_ = preferences_.begin("roadlink", false);
  return ready_;
}

void SettingsStore::load(AppSettings& settings) {
  if (!ready_) return;
  settings.simEnabled = preferences_.getBool("simEnabled", settings.simEnabled);
  // Ignore legacy simAuto: telemetry always requires an explicit START.
  settings.simSendGps = preferences_.getBool("simGps", settings.simSendGps);
  settings.simSendObd = preferences_.getBool("simObd", settings.simSendObd);
  settings.simSendIntervalMs =
      preferences_.getULong("simInterval", settings.simSendIntervalMs);
  settings.simSendIntervalMs = constrain(
      settings.simSendIntervalMs,
      AppConfig::SIM_SEND_INTERVAL_MIN_MS,
      AppConfig::SIM_SEND_INTERVAL_MAX_MS);
  settings.simObdFieldMask = preferences_.getUShort(
      "obdFields", settings.simObdFieldMask) & StreamField::ALL;
  settings.streamConfigRevision = preferences_.getULong(
      "streamRev", settings.streamConfigRevision);
  if (settings.streamConfigRevision == 0) settings.streamConfigRevision = 1;
  settings.simServerPort =
      preferences_.getUShort("simPort", settings.simServerPort);
  settings.simAccessKey =
      preferences_.getULong("simKey", settings.simAccessKey) % 1000000UL;
  for (uint8_t index = 0; index < 4; ++index) {
    const String key = "simIp" + String(index);
    settings.simServerIp[index] = preferences_.getUChar(
        key.c_str(), settings.simServerIp[index]);
  }
}

void SettingsStore::saveSim(const AppSettings& settings) {
  if (!ready_) return;
  preferences_.putBool("simEnabled", settings.simEnabled);
  preferences_.remove("simAuto");
  preferences_.putBool("simGps", settings.simSendGps);
  preferences_.putBool("simObd", settings.simSendObd);
  preferences_.putULong("simInterval", settings.simSendIntervalMs);
  preferences_.putUShort("obdFields", settings.simObdFieldMask & StreamField::ALL);
  preferences_.putULong("streamRev", settings.streamConfigRevision);
  preferences_.putUShort("simPort", settings.simServerPort);
  preferences_.putULong("simKey", settings.simAccessKey % 1000000UL);
  for (uint8_t index = 0; index < 4; ++index) {
    const String key = "simIp" + String(index);
    preferences_.putUChar(key.c_str(), settings.simServerIp[index]);
  }
}
