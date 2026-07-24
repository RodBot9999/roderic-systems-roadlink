#include "ObdService.h"

namespace {
constexpr uint8_t LIVE_PIDS[] = {
  0x0C, // Engine RPM
  0x0D, // Vehicle speed
  0x05, // Coolant temperature
  0x11, // Throttle position
  0x0B, // Intake manifold pressure
  0x0F, // Intake air temperature
  0x0E, // Ignition timing advance
  0x42, // Control module voltage
  0x2F  // Fuel level
};
constexpr uint8_t LIVE_PID_COUNT = sizeof(LIVE_PIDS) / sizeof(LIVE_PIDS[0]);
constexpr uint8_t SUPPORTED_BASES[] = {0x00, 0x20, 0x40, 0x60};
constexpr uint8_t SUPPORTED_BASE_COUNT = sizeof(SUPPORTED_BASES) / sizeof(SUPPORTED_BASES[0]);
}

ObdService::ObdService(CanService& can)
  : can_(can) {}

void ObdService::begin(uint16_t pollIntervalMs) {
  setPollInterval(pollIntervalMs);
}

void ObdService::update() {
  CanFrameSnapshot frame;
  while (can_.popDiagnosticFrame(frame)) {
    processDiagnosticFrame(frame);
  }

  updatePendingTimeout();

  switch (operation_) {
    case ObdOperation::Discovery:     updateDiscovery(); break;
    case ObdOperation::SupportedPids: updateSupportedScan(); break;
    case ObdOperation::LiveData:      updateLivePolling(); break;
    default:                          break;
  }
}

void ObdService::setPollInterval(uint16_t intervalMs) {
  pollIntervalMs_ = constrain(intervalMs, 50, 2000);
}

uint16_t ObdService::pollInterval() const {
  return pollIntervalMs_;
}

bool ObdService::prepareRequest(const char* label) {
  if (!can_.initialized()) {
    finishOperation(ObdResult::CanOffline, String(label) + ": CAN offline");
    return false;
  }

  if (!can_.canTransmit()) {
    finishOperation(ObdResult::TxFailed, String(label) + ": CAN not in Normal mode");
    return false;
  }

  pending_ = PendingRequest{};
  isoTp_ = IsoTpAssembly{};
  lastNegativeResponseCode_ = 0;
  return true;
}

bool ObdService::startDiscovery() {
  if (!prepareRequest("Discovery")) return false;

  for (uint8_t index = 0; index < AppConfig::OBD_MAX_ECUS; ++index) {
    ecus_[index] = ObdEcuInfo{};
  }
  ecuCount_ = 0;
  operation_ = ObdOperation::Discovery;
  result_ = ObdResult::Busy;
  statusMessage_ = "Broadcasting Mode 01 PID 00";
  operationDeadlineMs_ = millis() + AppConfig::OBD_DISCOVERY_WINDOW_MS;
  return sendRequest(0x01, 0x00, 0x7DF, true);
}

bool ObdService::startSupportedPidScan() {
  if (!prepareRequest("Supported PID scan")) return false;

  for (uint8_t ecuIndex = 0; ecuIndex < ecuCount_; ++ecuIndex) {
    for (uint8_t maskIndex = 0; maskIndex < 4; ++maskIndex) {
      ecus_[ecuIndex].supportedMasks[maskIndex] = 0;
      ecus_[ecuIndex].supportedMaskValid[maskIndex] = false;
    }
  }

  operation_ = ObdOperation::SupportedPids;
  result_ = ObdResult::Busy;
  statusMessage_ = "Scanning PID support";
  supportedBaseIndex_ = 0;
  supportedWindowDeadlineMs_ = 0;
  pending_ = PendingRequest{};
  return true;
}

bool ObdService::setLivePolling(bool enabled) {
  if (!enabled) {
    livePollingEnabled_ = false;
    liveOneShotActive_ = false;
    if (operation_ == ObdOperation::LiveData) {
      pending_ = PendingRequest{};
      finishOperation(ObdResult::Idle, "Live polling stopped");
    }
    return true;
  }

  if (!prepareRequest("Live data")) return false;

  livePollingEnabled_ = true;
  liveOneShotActive_ = false;
  livePidIndex_ = 0;
  operation_ = ObdOperation::LiveData;
  result_ = ObdResult::Busy;
  statusMessage_ = "Live OBD polling active";
  lastLiveRequestMs_ = 0;
  return true;
}

bool ObdService::requestLiveDataOnce() {
  if (!prepareRequest("Live data request")) return false;

  livePollingEnabled_ = false;
  liveOneShotActive_ = true;
  liveOneShotRemaining_ = LIVE_PID_COUNT;
  livePidIndex_ = 0;
  operation_ = ObdOperation::LiveData;
  result_ = ObdResult::Busy;
  statusMessage_ = "Reading live PIDs once";
  lastLiveRequestMs_ = 0;
  return true;
}

bool ObdService::startReadDtcs() {
  if (!prepareRequest("Read DTCs")) return false;
  clearDtcs();
  operation_ = ObdOperation::ReadDtcs;
  result_ = ObdResult::Busy;
  statusMessage_ = "Requesting stored DTCs";
  return sendRequest(0x03, -1, activeRequestId());
}

bool ObdService::startClearDtcs() {
  if (!prepareRequest("Clear DTCs")) return false;
  operation_ = ObdOperation::ClearDtcs;
  result_ = ObdResult::Busy;
  statusMessage_ = "Clear command sent";
  return sendRequest(0x04, -1, activeRequestId());
}

bool ObdService::startReadVin() {
  if (!prepareRequest("Read VIN")) return false;
  vin_ = "";
  operation_ = ObdOperation::ReadVin;
  result_ = ObdResult::Busy;
  statusMessage_ = "Requesting VIN";
  return sendRequest(0x09, 0x02, activeRequestId());
}

void ObdService::cancelOperation() {
  pending_ = PendingRequest{};
  isoTp_ = IsoTpAssembly{};
  livePollingEnabled_ = false;
  liveOneShotActive_ = false;
  operation_ = ObdOperation::Idle;
  result_ = ObdResult::Idle;
  statusMessage_ = "Cancelled";
}

bool ObdService::sendRequest(
    uint8_t service,
    int16_t pid,
    uint32_t requestId,
    bool collectMultiple) {
  uint8_t payload[2] = {service, 0};
  uint8_t payloadLength = 1;

  if (pid >= 0) {
    payload[1] = static_cast<uint8_t>(pid);
    payloadLength = 2;
  }

  if (!sendSingleFramePayload(payload, payloadLength, requestId)) {
    statistics_.txFailures++;
    finishOperation(ObdResult::TxFailed, "CAN transmit failed");
    return false;
  }

  pending_.active = true;
  pending_.service = service;
  pending_.pid = pid;
  pending_.requestId = requestId;
  pending_.sentMs = millis();
  pending_.timeoutMs = AppConfig::OBD_RESPONSE_TIMEOUT_MS;
  pending_.collectMultiple = collectMultiple;
  statistics_.requestsSent++;
  return true;
}

bool ObdService::sendSingleFramePayload(
    const uint8_t* payload,
    uint8_t payloadLength,
    uint32_t requestId) {
  if (payload == nullptr || payloadLength == 0 || payloadLength > 7) {
    return false;
  }

  uint8_t frame[8] = {};
  frame[0] = payloadLength;
  for (uint8_t index = 0; index < payloadLength; ++index) {
    frame[index + 1] = payload[index];
  }

  return can_.sendFrame(requestId, false, 8, frame);
}

bool ObdService::sendFlowControl(uint32_t responseId) {
  if (responseId < 0x7E8 || responseId > 0x7EF) {
    return false;
  }

  const uint32_t requestId = responseId - 8;
  const uint8_t frame[8] = {0x30, 0x00, 0x00, 0, 0, 0, 0, 0};
  const bool success = can_.sendFrame(requestId, false, 8, frame);
  if (success) statistics_.flowControlFrames++;
  return success;
}

void ObdService::processDiagnosticFrame(const CanFrameSnapshot& frame) {
  if (!frame.valid || frame.extended || frame.remote || frame.dlc == 0) {
    return;
  }

  findOrCreateEcu(frame.id);
  lastResponseId_ = frame.id;
  statistics_.responsesReceived++;

  const uint8_t frameType = frame.data[0] >> 4;
  switch (frameType) {
    case 0x0: processIsoTpSingleFrame(frame); break;
    case 0x1: processIsoTpFirstFrame(frame); break;
    case 0x2: processIsoTpConsecutiveFrame(frame); break;
    default:  break;
  }
}

void ObdService::processIsoTpSingleFrame(const CanFrameSnapshot& frame) {
  const uint8_t length = frame.data[0] & 0x0F;
  if (length == 0 || length > 7 || length + 1 > frame.dlc) {
    finishOperation(ObdResult::ProtocolError, "Invalid ISO-TP single frame");
    return;
  }

  processPayload(frame.id, &frame.data[1], length);
}

void ObdService::processIsoTpFirstFrame(const CanFrameSnapshot& frame) {
  if (frame.dlc < 3) return;

  const uint16_t totalLength =
      static_cast<uint16_t>((frame.data[0] & 0x0F) << 8) | frame.data[1];

  if (totalLength == 0 || totalLength > AppConfig::OBD_ISOTP_BUFFER_SIZE) {
    finishOperation(ObdResult::ProtocolError, "ISO-TP payload too large");
    return;
  }

  isoTp_ = IsoTpAssembly{};
  isoTp_.active = true;
  isoTp_.responseId = frame.id;
  isoTp_.totalLength = totalLength;
  isoTp_.startedMs = millis();
  isoTp_.nextSequence = 1;

  for (uint8_t index = 2; index < frame.dlc &&
       isoTp_.receivedLength < isoTp_.totalLength; ++index) {
    isoTp_.payload[isoTp_.receivedLength++] = frame.data[index];
  }

  if (!sendFlowControl(frame.id)) {
    isoTp_.active = false;
    finishOperation(ObdResult::TxFailed, "ISO-TP flow control failed");
  }
}

void ObdService::processIsoTpConsecutiveFrame(const CanFrameSnapshot& frame) {
  if (!isoTp_.active || frame.id != isoTp_.responseId || frame.dlc < 2) {
    return;
  }

  const uint8_t sequence = frame.data[0] & 0x0F;
  if (sequence != (isoTp_.nextSequence & 0x0F)) {
    isoTp_.active = false;
    finishOperation(ObdResult::ProtocolError, "ISO-TP sequence mismatch");
    return;
  }

  isoTp_.nextSequence++;
  for (uint8_t index = 1; index < frame.dlc &&
       isoTp_.receivedLength < isoTp_.totalLength; ++index) {
    isoTp_.payload[isoTp_.receivedLength++] = frame.data[index];
  }

  if (isoTp_.receivedLength >= isoTp_.totalLength) {
    isoTp_.active = false;
    processPayload(
        isoTp_.responseId,
        isoTp_.payload,
        isoTp_.totalLength);
  }
}

void ObdService::processPayload(
    uint32_t responseId,
    const uint8_t* payload,
    uint16_t length) {
  if (payload == nullptr || length == 0) return;

  if (payload[0] == 0x7F && length >= 3) {
    lastNegativeResponseCode_ = payload[2];
    statistics_.negativeResponses++;
    pending_ = PendingRequest{};
    finishOperation(
        ObdResult::NegativeResponse,
        "Negative response NRC 0x" + String(payload[2], HEX));
    return;
  }

  const uint8_t responseService = payload[0];
  const int16_t responsePid = length >= 2 ? payload[1] : -1;

  if (responseService == 0x41 && length >= 2) {
    const uint8_t pid = payload[1];
    if (pid == 0x00 || pid == 0x20 || pid == 0x40 || pid == 0x60) {
      parseSupportedMask(responseId, pid, &payload[2], length - 2);
    } else {
      parseLivePid(pid, &payload[2], length - 2);
    }
  } else if (responseService == 0x43) {
    parseDtcPayload(payload, length);
  } else if (responseService == 0x44) {
    clearDtcs();
    finishOperation(ObdResult::Success, "DTC clear acknowledged");
  } else if (responseService == 0x49 && length >= 2 && payload[1] == 0x02) {
    parseVinPayload(payload, length);
  }

  if (payloadMatchesPending(responseService, responsePid) && !pending_.collectMultiple) {
    pending_ = PendingRequest{};
  }
}

ObdEcuInfo* ObdService::findOrCreateEcu(uint32_t responseId) {
  for (uint8_t index = 0; index < ecuCount_; ++index) {
    if (ecus_[index].used && ecus_[index].responseId == responseId) {
      ecus_[index].lastSeenMs = millis();
      return &ecus_[index];
    }
  }

  if (ecuCount_ >= AppConfig::OBD_MAX_ECUS) return nullptr;

  ObdEcuInfo& ecu = ecus_[ecuCount_++];
  ecu.used = true;
  ecu.responseId = responseId;
  ecu.requestId = responseId >= 8 ? responseId - 8 : 0x7DF;
  ecu.lastSeenMs = millis();
  return &ecu;
}

uint32_t ObdService::activeRequestId() const {
  const ObdEcuInfo* ecuInfo = primaryEcu();
  return ecuInfo != nullptr ? ecuInfo->requestId : 0x7DF;
}

void ObdService::updatePendingTimeout() {
  if (!pending_.active) return;

  if (millis() - pending_.sentMs < pending_.timeoutMs) return;

  pending_ = PendingRequest{};
  statistics_.timeouts++;

  if (operation_ == ObdOperation::LiveData) {
    // The live scheduler advances to the next PID after each request, even
    // when a specific ECU does not answer that PID.
    statusMessage_ = "PID timeout; continuing";
    return;
  }

  if (operation_ != ObdOperation::Discovery &&
      operation_ != ObdOperation::SupportedPids) {
    finishOperation(ObdResult::Timeout, "OBD response timeout");
  }
}

void ObdService::updateDiscovery() {
  if (millis() < operationDeadlineMs_) return;

  pending_ = PendingRequest{};
  if (ecuCount_ > 0) {
    finishOperation(
        ObdResult::Success,
        "Found " + String(ecuCount_) + " responding ECU(s)");
  } else {
    finishOperation(ObdResult::Timeout, "No OBD ECU response");
  }
}

void ObdService::updateSupportedScan() {
  if (supportedBaseIndex_ >= SUPPORTED_BASE_COUNT) {
    pending_ = PendingRequest{};
    finishOperation(ObdResult::Success, "Supported PID scan complete");
    return;
  }

  if (supportedWindowDeadlineMs_ != 0) {
    if (millis() >= supportedWindowDeadlineMs_) {
      pending_ = PendingRequest{};
      supportedWindowDeadlineMs_ = 0;
      supportedBaseIndex_++;
    }
    return;
  }

  if (pending_.active) return;

  const uint8_t basePid = SUPPORTED_BASES[supportedBaseIndex_];
  if (sendRequest(0x01, basePid, activeRequestId(), true)) {
    supportedWindowDeadlineMs_ = millis() + AppConfig::OBD_SUPPORTED_WINDOW_MS;
    statusMessage_ = "Scanning PID block 0x" + String(basePid, HEX);
  }
}

void ObdService::updateLivePolling() {
  if (!livePollingEnabled_ && !liveOneShotActive_) {
    return;
  }

  if (pending_.active) return;

  if (liveOneShotActive_ && liveOneShotRemaining_ == 0) {
    liveOneShotActive_ = false;
    finishOperation(ObdResult::Success, "Live PID snapshot complete");
    return;
  }

  if (millis() - lastLiveRequestMs_ < pollIntervalMs_) return;

  const uint8_t pid = LIVE_PIDS[livePidIndex_];
  if (sendRequest(0x01, pid, activeRequestId())) {
    lastLiveRequestMs_ = millis();
    livePidIndex_ = (livePidIndex_ + 1) % LIVE_PID_COUNT;
    if (liveOneShotActive_ && liveOneShotRemaining_ > 0) {
      liveOneShotRemaining_--;
    }
  }
}

void ObdService::finishOperation(ObdResult result, const String& message) {
  result_ = result;
  statusMessage_ = message;

  if (operation_ != ObdOperation::LiveData ||
      (!livePollingEnabled_ && !liveOneShotActive_)) {
    operation_ = ObdOperation::Idle;
  }
}

void ObdService::clearDtcs() {
  dtcCount_ = 0;
  for (uint8_t index = 0; index < AppConfig::OBD_MAX_DTCS; ++index) {
    dtcs_[index] = ObdDtc{};
  }
}

void ObdService::decodeDtc(uint8_t first, uint8_t second, char output[6]) const {
  static const char TYPES[] = {'P', 'C', 'B', 'U'};
  output[0] = TYPES[(first >> 6) & 0x03];
  output[1] = static_cast<char>('0' + ((first >> 4) & 0x03));
  const uint8_t digit2 = first & 0x0F;
  const uint8_t digit3 = (second >> 4) & 0x0F;
  const uint8_t digit4 = second & 0x0F;
  output[2] = digit2 < 10 ? '0' + digit2 : 'A' + digit2 - 10;
  output[3] = digit3 < 10 ? '0' + digit3 : 'A' + digit3 - 10;
  output[4] = digit4 < 10 ? '0' + digit4 : 'A' + digit4 - 10;
  output[5] = '\0';
}

void ObdService::parseLivePid(
    uint8_t pid,
    const uint8_t* data,
    uint16_t length) {
  if (data == nullptr || length == 0) return;

  switch (pid) {
    case 0x05:
      liveData_.coolantC = static_cast<float>(data[0]) - 40.0f;
      liveData_.coolantValid = true;
      break;
    case 0x0B:
      liveData_.mapKpa = data[0];
      liveData_.mapValid = true;
      break;
    case 0x0C:
      if (length >= 2) {
        liveData_.rpm = ((data[0] * 256.0f) + data[1]) / 4.0f;
        liveData_.rpmValid = true;
      }
      break;
    case 0x0D:
      liveData_.speedKmh = data[0];
      liveData_.speedValid = true;
      break;
    case 0x0E:
      liveData_.timingDegrees = data[0] / 2.0f - 64.0f;
      liveData_.timingValid = true;
      break;
    case 0x0F:
      liveData_.intakeTempC = static_cast<float>(data[0]) - 40.0f;
      liveData_.intakeTempValid = true;
      break;
    case 0x11:
      liveData_.throttlePercent = data[0] * 100.0f / 255.0f;
      liveData_.throttleValid = true;
      break;
    case 0x2F:
      liveData_.fuelLevelPercent = data[0] * 100.0f / 255.0f;
      liveData_.fuelLevelValid = true;
      break;
    case 0x42:
      if (length >= 2) {
        liveData_.moduleVoltage = (data[0] * 256.0f + data[1]) / 1000.0f;
        liveData_.voltageValid = true;
      }
      break;
  }

  liveData_.lastUpdateMs = millis();
  result_ = ObdResult::Success;
  statusMessage_ = livePollingEnabled_ ? "Live OBD polling active" : "Live PID received";
}

void ObdService::parseSupportedMask(
    uint32_t responseId,
    uint8_t basePid,
    const uint8_t* data,
    uint16_t length) {
  if (data == nullptr || length < 4) return;

  ObdEcuInfo* ecuInfo = findOrCreateEcu(responseId);
  if (ecuInfo == nullptr) return;

  const uint8_t index = basePid / 0x20;
  if (index >= 4) return;

  ecuInfo->supportedMasks[index] =
      (static_cast<uint32_t>(data[0]) << 24) |
      (static_cast<uint32_t>(data[1]) << 16) |
      (static_cast<uint32_t>(data[2]) << 8) |
      data[3];
  ecuInfo->supportedMaskValid[index] = true;
}

void ObdService::parseDtcPayload(const uint8_t* payload, uint16_t length) {
  clearDtcs();

  for (uint16_t index = 1;
       index + 1 < length && dtcCount_ < AppConfig::OBD_MAX_DTCS;
       index += 2) {
    const uint8_t first = payload[index];
    const uint8_t second = payload[index + 1];
    if (first == 0 && second == 0) continue;

    ObdDtc& dtcEntry = dtcs_[dtcCount_++];
    dtcEntry.valid = true;
    decodeDtc(first, second, dtcEntry.code);
  }

  finishOperation(
      ObdResult::Success,
      dtcCount_ == 0 ? "No stored DTCs" : String(dtcCount_) + " DTC(s) received");
}

void ObdService::parseVinPayload(const uint8_t* payload, uint16_t length) {
  vin_ = "";

  uint16_t start = 2;
  if (length >= 3 && payload[2] <= 0x05) {
    start = 3; // Skip VIN message counter byte used by Mode 09 PID 02.
  }

  for (uint16_t index = start; index < length && vin_.length() < 17; ++index) {
    const char character = static_cast<char>(payload[index]);
    if (character >= 32 && character <= 126) vin_ += character;
  }

  finishOperation(
      vin_.length() >= 11 ? ObdResult::Success : ObdResult::ProtocolError,
      vin_.length() ? "VIN received" : "VIN payload empty");
}

bool ObdService::payloadMatchesPending(
    uint8_t responseService,
    int16_t pid) const {
  if (!pending_.active) return false;

  const uint8_t expectedService = pending_.service + 0x40;
  if (responseService != expectedService) return false;
  if (pending_.pid >= 0 && pid != pending_.pid) return false;
  return true;
}

bool ObdService::livePollingEnabled() const { return livePollingEnabled_; }
bool ObdService::busy() const { return result_ == ObdResult::Busy || pending_.active; }
ObdOperation ObdService::operation() const { return operation_; }
ObdResult ObdService::result() const { return result_; }
const String& ObdService::statusMessage() const { return statusMessage_; }
uint8_t ObdService::ecuCount() const { return ecuCount_; }

const ObdEcuInfo* ObdService::ecu(uint8_t index) const {
  return index < ecuCount_ ? &ecus_[index] : nullptr;
}

const ObdEcuInfo* ObdService::primaryEcu() const {
  return ecuCount_ > 0 ? &ecus_[0] : nullptr;
}

const ObdLiveData& ObdService::liveData() const { return liveData_; }
uint8_t ObdService::dtcCount() const { return dtcCount_; }

const ObdDtc* ObdService::dtc(uint8_t index) const {
  return index < dtcCount_ ? &dtcs_[index] : nullptr;
}

const String& ObdService::vin() const { return vin_; }
uint32_t ObdService::lastResponseId() const { return lastResponseId_; }
uint8_t ObdService::lastNegativeResponseCode() const { return lastNegativeResponseCode_; }
const ObdStatistics& ObdService::statistics() const { return statistics_; }

const char* ObdService::operationLabel(ObdOperation value) {
  switch (value) {
    case ObdOperation::Idle:          return "Idle";
    case ObdOperation::Discovery:     return "ECU discovery";
    case ObdOperation::SupportedPids: return "Supported PIDs";
    case ObdOperation::LiveData:      return "Live data";
    case ObdOperation::ReadDtcs:      return "Read DTCs";
    case ObdOperation::ClearDtcs:     return "Clear DTCs";
    case ObdOperation::ReadVin:       return "Read VIN";
  }
  return "Unknown";
}

const char* ObdService::resultLabel(ObdResult value) {
  switch (value) {
    case ObdResult::Idle:             return "Idle";
    case ObdResult::Busy:             return "Busy";
    case ObdResult::Success:          return "Success";
    case ObdResult::Timeout:          return "Timeout";
    case ObdResult::CanOffline:       return "CAN offline";
    case ObdResult::TxFailed:         return "TX failed";
    case ObdResult::NegativeResponse: return "Negative response";
    case ObdResult::ProtocolError:    return "Protocol error";
  }
  return "Unknown";
}
