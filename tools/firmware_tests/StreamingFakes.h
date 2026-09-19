// Fake hardware/persistence/transport boundaries; GPS, OBD and the streaming
// controller themselves are compiled from the production sources.
class CanService {
public:
  bool online = true;
  bool transmit = false;
  std::vector<CanFrameSnapshot> sent;
  bool initialized() const { return online; }
  bool canTransmit() const { return online && transmit; }
  bool reinitialize(CanBitrate, CanOperatingMode mode) {
    transmit = mode == CanOperatingMode::Normal;
    return online;
  }
  bool popDiagnosticFrame(CanFrameSnapshot&) { return false; }
  bool sendFrame(uint32_t id, bool extended, uint8_t dlc,
      const uint8_t* data, bool remote = false) {
    if (!canTransmit()) return false;
    CanFrameSnapshot frame;
    frame.id = id; frame.extended = extended; frame.dlc = dlc;
    memcpy(frame.data, data, dlc); sent.push_back(frame);
    return true;
  }
};
class SettingsStore {
public:
  int saves = 0;
  AppSettings saved;
  void saveSim(const AppSettings& settings) { saved = settings; ++saves; }
};
class A7670Service {
public:
  bool active = false;
  bool available = true;
  int syncs = 0;
  RemoteStreamingConfig pending;
  struct Snapshot { String lastError = "Modem unavailable"; } data;
  bool ready() const { return available; }
  bool telemetryRunning() const { return active; }
  bool startTelemetry() { return active = available; }
  void stopTelemetry() { active = false; }
  const Snapshot& snapshot() const { return data; }
  void setPayloadSelection(bool, bool) {}
  void setSendInterval(uint32_t) {}
  void setConfigSnapshot(uint32_t, uint16_t) { ++syncs; }
  bool takeRemoteConfig(RemoteStreamingConfig& config) {
    if (!pending.valid) return false;
    config = pending; pending.valid = false; return true;
  }
};
