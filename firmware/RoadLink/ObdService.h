#pragma once

#include <Arduino.h>
#include "AppConfig.h"
#include "CanService.h"

enum class ObdOperation : uint8_t {
  Idle,
  Discovery,
  SupportedPids,
  LiveData,
  ReadDtcs,
  ClearDtcs,
  ReadVin
};

enum class ObdResult : uint8_t {
  Idle,
  Busy,
  Success,
  Timeout,
  CanOffline,
  TxFailed,
  NegativeResponse,
  ProtocolError
};

struct ObdEcuInfo {
  bool used = false;
  uint32_t responseId = 0;
  uint32_t requestId = 0;
  uint32_t lastSeenMs = 0;
  uint32_t supportedMasks[4] = {};
  bool supportedMaskValid[4] = {};
};

struct ObdLiveData {
  bool rpmValid = false;
  float rpm = 0.0f;

  bool speedValid = false;
  float speedKmh = 0.0f;

  bool coolantValid = false;
  float coolantC = 0.0f;

  bool throttleValid = false;
  float throttlePercent = 0.0f;

  bool mapValid = false;
  float mapKpa = 0.0f;

  bool intakeTempValid = false;
  float intakeTempC = 0.0f;

  bool timingValid = false;
  float timingDegrees = 0.0f;

  bool voltageValid = false;
  float moduleVoltage = 0.0f;

  bool fuelLevelValid = false;
  float fuelLevelPercent = 0.0f;

  uint32_t lastUpdateMs = 0;
};

struct ObdDtc {
  bool valid = false;
  char code[6] = {};
};

struct ObdStatistics {
  uint32_t requestsSent = 0;
  uint32_t responsesReceived = 0;
  uint32_t timeouts = 0;
  uint32_t txFailures = 0;
  uint32_t negativeResponses = 0;
  uint32_t flowControlFrames = 0;
};

class ObdService {
public:
  explicit ObdService(CanService& can);

  void begin(uint16_t pollIntervalMs);
  void update();

  void setPollInterval(uint16_t intervalMs);
  uint16_t pollInterval() const;

  bool startDiscovery();
  bool startSupportedPidScan();
  bool setLivePolling(bool enabled);
  bool requestLiveDataOnce();
  bool startReadDtcs();
  bool startClearDtcs();
  bool startReadVin();
  void cancelOperation();

  bool livePollingEnabled() const;
  bool busy() const;
  ObdOperation operation() const;
  ObdResult result() const;
  const String& statusMessage() const;

  uint8_t ecuCount() const;
  const ObdEcuInfo* ecu(uint8_t index) const;
  const ObdEcuInfo* primaryEcu() const;

  const ObdLiveData& liveData() const;
  uint8_t dtcCount() const;
  const ObdDtc* dtc(uint8_t index) const;
  const String& vin() const;
  uint32_t lastResponseId() const;
  uint8_t lastNegativeResponseCode() const;
  const ObdStatistics& statistics() const;

  static const char* operationLabel(ObdOperation value);
  static const char* resultLabel(ObdResult value);

private:
  struct PendingRequest {
    bool active = false;
    uint8_t service = 0;
    int16_t pid = -1;
    uint32_t requestId = 0x7DF;
    uint32_t sentMs = 0;
    uint32_t timeoutMs = AppConfig::OBD_RESPONSE_TIMEOUT_MS;
    bool collectMultiple = false;
  };

  struct IsoTpAssembly {
    bool active = false;
    uint32_t responseId = 0;
    uint16_t totalLength = 0;
    uint16_t receivedLength = 0;
    uint8_t nextSequence = 1;
    uint32_t startedMs = 0;
    uint8_t payload[AppConfig::OBD_ISOTP_BUFFER_SIZE] = {};
  };

  bool prepareRequest(const char* label);
  bool sendRequest(uint8_t service, int16_t pid, uint32_t requestId, bool collectMultiple = false);
  bool sendSingleFramePayload(const uint8_t* payload, uint8_t payloadLength, uint32_t requestId);
  bool sendFlowControl(uint32_t responseId);

  void processDiagnosticFrame(const CanFrameSnapshot& frame);
  void processIsoTpSingleFrame(const CanFrameSnapshot& frame);
  void processIsoTpFirstFrame(const CanFrameSnapshot& frame);
  void processIsoTpConsecutiveFrame(const CanFrameSnapshot& frame);
  void processPayload(uint32_t responseId, const uint8_t* payload, uint16_t length);

  ObdEcuInfo* findOrCreateEcu(uint32_t responseId);
  uint32_t activeRequestId() const;
  void updatePendingTimeout();
  void updateDiscovery();
  void updateSupportedScan();
  void updateLivePolling();
  void finishOperation(ObdResult result, const String& message);
  void clearDtcs();
  void decodeDtc(uint8_t first, uint8_t second, char output[6]) const;
  void parseLivePid(uint8_t pid, const uint8_t* data, uint16_t length);
  void parseSupportedMask(uint32_t responseId, uint8_t basePid, const uint8_t* data, uint16_t length);
  void parseDtcPayload(const uint8_t* payload, uint16_t length);
  void parseVinPayload(const uint8_t* payload, uint16_t length);
  bool payloadMatchesPending(uint8_t responseService, int16_t pid) const;

  CanService& can_;

  uint16_t pollIntervalMs_ = AppConfig::OBD_POLL_DEFAULT_MS;
  bool livePollingEnabled_ = false;
  bool liveOneShotActive_ = false;
  uint8_t livePidIndex_ = 0;
  uint8_t liveOneShotRemaining_ = 0;
  uint32_t lastLiveRequestMs_ = 0;

  ObdOperation operation_ = ObdOperation::Idle;
  ObdResult result_ = ObdResult::Idle;
  String statusMessage_ = "Idle";

  PendingRequest pending_;
  IsoTpAssembly isoTp_;

  ObdEcuInfo ecus_[AppConfig::OBD_MAX_ECUS] = {};
  uint8_t ecuCount_ = 0;
  ObdLiveData liveData_;
  ObdDtc dtcs_[AppConfig::OBD_MAX_DTCS] = {};
  uint8_t dtcCount_ = 0;
  String vin_;
  uint32_t lastResponseId_ = 0;
  uint8_t lastNegativeResponseCode_ = 0;
  ObdStatistics statistics_;

  uint32_t operationDeadlineMs_ = 0;
  uint8_t supportedBaseIndex_ = 0;
  uint32_t supportedWindowDeadlineMs_ = 0;
};
