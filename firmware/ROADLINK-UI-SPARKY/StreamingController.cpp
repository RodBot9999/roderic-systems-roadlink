#include "StreamingController.h"

StreamingController::StreamingController(
    CanService& can,
    ObdService& obd,
    A7670Service& modem,
    AppSettings& settings,
    SettingsStore& settingsStore)
  : can_(can),
    obd_(obd),
    modem_(modem),
    settings_(settings),
    settingsStore_(settingsStore) {}

void StreamingController::begin() {
  settings_.simObdFieldMask &= StreamField::ALL;
  if (settings_.streamConfigRevision == 0) settings_.streamConfigRevision = 1;
  synchronizeServices();
  statusMessage_ = "Ready. Press START when configured.";
}

void StreamingController::update() {
  RemoteStreamingConfig remote;
  if (modem_.takeRemoteConfig(remote)) applyRemoteConfig(remote);

  if (pendingRemoteStart_ && !running() && modem_.ready() &&
      millis() - lastStartRetryMs_ >= 5000) {
    lastStartRetryMs_ = millis();
    startInternal(true);
  }

  const bool shouldPoll = running() && settings_.simSendObd &&
      settings_.simObdFieldMask != 0;
  if (shouldPoll && !obd_.livePollingEnabled() && !obd_.busy() &&
      ensureObdTransmitMode()) {
    ownsObdPolling_ = obd_.setLivePolling(true);
  } else if (!shouldPoll && ownsObdPolling_) {
    obd_.setLivePolling(false);
    ownsObdPolling_ = false;
  }
}

bool StreamingController::start() {
  pendingRemoteStart_ = false;
  if (!startInternal(false)) return false;
  commitLocalChange();
  statusMessage_ = "Streaming started";
  return true;
}

bool StreamingController::startInternal(bool allowRetry) {
  if (running()) {
    pendingRemoteStart_ = false;
    return true;
  }

  if (settings_.simSendObd && settings_.simObdFieldMask != 0 &&
      !ensureObdTransmitMode()) {
    statusMessage_ = "START failed: CAN controller unavailable";
    if (!allowRetry) Serial.println(F("[ERROR][STREAM] CAN unavailable for selected OBD fields"));
    pendingRemoteStart_ = allowRetry;
    synchronizeServices();
    return false;
  }

  obd_.setLivePidMask(settings_.simObdFieldMask);
  if (settings_.simSendObd && settings_.simObdFieldMask != 0) {
    ownsObdPolling_ = obd_.setLivePolling(true);
    if (!ownsObdPolling_) {
      statusMessage_ = "START failed: OBD polling unavailable";
      pendingRemoteStart_ = allowRetry;
      synchronizeServices();
      return false;
    }
  }

  if (!modem_.startTelemetry()) {
    if (ownsObdPolling_) obd_.setLivePolling(false);
    ownsObdPolling_ = false;
    statusMessage_ = modem_.snapshot().lastError;
    pendingRemoteStart_ = allowRetry;
    synchronizeServices();
    return false;
  }

  pendingRemoteStart_ = false;
  synchronizeServices();
  statusMessage_ = "Streaming started";
  return true;
}

void StreamingController::stop() {
  pendingRemoteStart_ = false;
  const bool changed = running();
  modem_.stopTelemetry();
  if (ownsObdPolling_) obd_.setLivePolling(false);
  ownsObdPolling_ = false;
  if (changed) commitLocalChange();
  else synchronizeServices();
  statusMessage_ = "Streaming stopped; heartbeat remains active";
}

bool StreamingController::running() const {
  return modem_.telemetryRunning();
}

void StreamingController::setGpsEnabled(bool enabled) {
  if (settings_.simSendGps == enabled) return;
  settings_.simSendGps = enabled;
  commitLocalChange();
}

void StreamingController::setObdEnabled(bool enabled) {
  if (settings_.simSendObd == enabled) return;
  settings_.simSendObd = enabled;
  commitLocalChange();
}

void StreamingController::toggleObdField(uint16_t field) {
  field &= StreamField::ALL;
  if (field == 0) return;
  settings_.simObdFieldMask ^= field;
  commitLocalChange();
}

void StreamingController::setIntervalMs(uint32_t intervalMs) {
  intervalMs = constrain(
      intervalMs,
      AppConfig::SIM_SEND_INTERVAL_MIN_MS,
      AppConfig::SIM_SEND_INTERVAL_MAX_MS);
  if (settings_.simSendIntervalMs == intervalMs) return;
  settings_.simSendIntervalMs = intervalMs;
  commitLocalChange();
}

void StreamingController::configurationChanged() {
  commitLocalChange();
}

uint8_t StreamingController::selectedObdFieldCount() const {
  uint16_t mask = settings_.simObdFieldMask & StreamField::ALL;
  uint8_t count = 0;
  while (mask) {
    count += mask & 1U;
    mask >>= 1;
  }
  return count;
}

const String& StreamingController::statusMessage() const {
  return statusMessage_;
}

bool StreamingController::ensureObdTransmitMode() {
  if (!can_.initialized()) return false;
  if (settings_.canMode == CanOperatingMode::Normal && can_.canTransmit()) return true;
  settings_.canMode = CanOperatingMode::Normal;
  return can_.reinitialize(settings_.canBitrate, settings_.canMode);
}

void StreamingController::synchronizeServices() {
  obd_.setLivePidMask(settings_.simObdFieldMask);
  modem_.setPayloadSelection(settings_.simSendGps, settings_.simSendObd);
  modem_.setSendInterval(settings_.simSendIntervalMs);
  modem_.setConfigSnapshot(
      settings_.streamConfigRevision,
      settings_.simObdFieldMask);
}

void StreamingController::commitLocalChange() {
  settings_.streamConfigRevision = settings_.streamConfigRevision == UINT32_MAX
      ? 1 : settings_.streamConfigRevision + 1;
  settingsStore_.saveSim(settings_);
  synchronizeServices();
}

void StreamingController::applyRemoteConfig(
    const RemoteStreamingConfig& remote) {
  if (!remote.valid || remote.revision <= settings_.streamConfigRevision) {
    return;
  }

  settings_.simSendGps = remote.sendGps;
  settings_.simSendObd = remote.sendObd;
  settings_.simObdFieldMask = remote.obdFieldMask & StreamField::ALL;
  settings_.simSendIntervalMs = constrain(
      remote.intervalSeconds * 1000UL,
      AppConfig::SIM_SEND_INTERVAL_MIN_MS,
      AppConfig::SIM_SEND_INTERVAL_MAX_MS);
  settings_.streamConfigRevision = remote.revision;
  settingsStore_.saveSim(settings_);
  synchronizeServices();

  if (remote.running) {
    pendingRemoteStart_ = true;
    lastStartRetryMs_ = millis() - 5000;
    startInternal(true);
  } else {
    pendingRemoteStart_ = false;
    modem_.stopTelemetry();
    if (ownsObdPolling_) obd_.setLivePolling(false);
    ownsObdPolling_ = false;
    synchronizeServices();
    statusMessage_ = "Remote configuration applied; streaming stopped";
  }
}
