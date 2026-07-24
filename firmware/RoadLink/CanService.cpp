#include "CanService.h"
#include <SPI.h>

CanService::CanService(uint8_t chipSelectPin, uint8_t interruptPin)
  : chipSelectPin_(chipSelectPin),
    interruptPin_(interruptPin),
    controller_(chipSelectPin) {}

bool CanService::begin(CanBitrate bitrate, CanOperatingMode mode) {
  pinMode(interruptPin_, INPUT_PULLUP);
  pinMode(chipSelectPin_, OUTPUT);
  digitalWrite(chipSelectPin_, HIGH);

  SPI.begin(
      Pins::CAN_SCK,
      Pins::CAN_MISO,
      Pins::CAN_MOSI,
      Pins::CAN_CS);

  return reinitialize(bitrate, mode);
}

bool CanService::reinitialize(CanBitrate bitrate, CanOperatingMode mode) {
  bitrate_ = bitrate;
  operatingMode_ = mode;

  initializationResult_ = controller_.begin(
      MCP_ANY,
      libraryBitrate(bitrate),
      MCP_8MHZ);

  initialized_ = initializationResult_ == CAN_OK;

  if (!initialized_) {
    return false;
  }

  const byte modeResult = controller_.setMode(libraryMode(mode));
  initialized_ = modeResult == CAN_OK;
  controllerError_ = controller_.checkError();
  rateWindowStartMs_ = millis();
  diagnosticQueueHead_ = 0;
  diagnosticQueueTail_ = 0;
  diagnosticQueueCount_ = 0;

  return initialized_;
}

bool CanService::setOperatingMode(CanOperatingMode mode) {
  if (!initialized_) {
    return false;
  }

  const byte result = controller_.setMode(libraryMode(mode));
  if (result == CAN_OK) {
    operatingMode_ = mode;
    return true;
  }

  return false;
}

byte CanService::libraryBitrate(CanBitrate bitrate) const {
  switch (bitrate) {
    case CanBitrate::K125:  return CAN_125KBPS;
    case CanBitrate::K250:  return CAN_250KBPS;
    case CanBitrate::K500:  return CAN_500KBPS;
    case CanBitrate::K1000: return CAN_1000KBPS;
  }
  return CAN_500KBPS;
}

byte CanService::libraryMode(CanOperatingMode mode) const {
  return mode == CanOperatingMode::ListenOnly
      ? MCP_LISTENONLY
      : MCP_NORMAL;
}

void CanService::update() {
  updateRateCounter();

  if (!initialized_) {
    return;
  }

  uint8_t processed = 0;
  while (processed < AppConfig::CAN_MAX_FRAMES_PER_LOOP &&
         controller_.checkReceive() == CAN_MSGAVAIL) {
    unsigned long rawId = 0;
    byte dlc = 0;
    byte data[8] = {};

    if (controller_.readMsgBuf(&rawId, &dlc, data) == CAN_OK) {
      processFrame(static_cast<uint32_t>(rawId), dlc, data);
    }
    processed++;
  }

  if (millis() - lastErrorPollMs_ >= 1000) {
    lastErrorPollMs_ = millis();
    controllerError_ = controller_.checkError();
  }
}

bool CanService::sendFrame(
    uint32_t id,
    bool extended,
    uint8_t dlc,
    const uint8_t* data,
    bool remote) {
  if (!initialized_ || operatingMode_ != CanOperatingMode::Normal ||
      data == nullptr || dlc > 8) {
    statistics_.transmitErrors++;
    lastTransmitResult_ = 0xFF;
    return false;
  }

  uint32_t rawId = id;
  if (extended) rawId |= 0x80000000UL;
  if (remote) rawId |= 0x40000000UL;

  byte buffer[8] = {};
  for (uint8_t index = 0; index < dlc; ++index) {
    buffer[index] = data[index];
  }

  lastTransmitResult_ = controller_.sendMsgBuf(rawId, dlc, buffer);
  const bool success = lastTransmitResult_ == CAN_OK;

  if (success) {
    statistics_.transmittedFrames++;
    lastTransmittedFrame_ = CanFrameSnapshot{};
    lastTransmittedFrame_.valid = true;
    lastTransmittedFrame_.extended = extended;
    lastTransmittedFrame_.remote = remote;
    lastTransmittedFrame_.id = id;
    lastTransmittedFrame_.dlc = dlc;
    lastTransmittedFrame_.timestampMs = millis();
    for (uint8_t index = 0; index < dlc; ++index) {
      lastTransmittedFrame_.data[index] = buffer[index];
    }

    if (serialStreaming_) {
      printFrameToSerial(lastTransmittedFrame_, true);
    }
  } else {
    statistics_.transmitErrors++;
  }

  return success;
}

void CanService::processFrame(uint32_t rawId, uint8_t dlc, const uint8_t* data) {
  CanFrameSnapshot frame;
  frame.valid = true;
  frame.extended = (rawId & 0x80000000UL) != 0;
  frame.remote = (rawId & 0x40000000UL) != 0;
  frame.id = frame.extended
      ? rawId & 0x1FFFFFFFUL
      : rawId & 0x7FFUL;
  frame.dlc = dlc > 8 ? 8 : dlc;
  frame.timestampMs = millis();

  for (uint8_t index = 0; index < frame.dlc; ++index) {
    frame.data[index] = data[index];
  }

  lastFrame_ = frame;
  statistics_.totalFrames++;
  framesInWindow_++;

  if (frame.extended) statistics_.extendedFrames++;
  else statistics_.standardFrames++;
  if (frame.remote) statistics_.remoteFrames++;

  trackIdentifier(frame);
  queueDiagnosticFrame(frame);

  if (serialStreaming_) {
    printFrameToSerial(frame, false);
  }
}

void CanService::trackIdentifier(const CanFrameSnapshot& frame) {
  for (uint8_t index = 0; index < statistics_.uniqueIdCount; ++index) {
    CanIdEntry& entry = idTable_[index];
    if (entry.used && entry.id == frame.id && entry.extended == frame.extended) {
      entry.count++;
      entry.lastSeenMs = frame.timestampMs;
      entry.dlc = frame.dlc;
      for (uint8_t byteIndex = 0; byteIndex < frame.dlc; ++byteIndex) {
        entry.lastData[byteIndex] = frame.data[byteIndex];
      }
      return;
    }
  }

  if (statistics_.uniqueIdCount >= AppConfig::CAN_MAX_TRACKED_IDS) {
    statistics_.tableOverflowCount++;
    return;
  }

  CanIdEntry& entry = idTable_[statistics_.uniqueIdCount];
  entry.used = true;
  entry.extended = frame.extended;
  entry.id = frame.id;
  entry.count = 1;
  entry.lastSeenMs = frame.timestampMs;
  entry.dlc = frame.dlc;
  for (uint8_t byteIndex = 0; byteIndex < frame.dlc; ++byteIndex) {
    entry.lastData[byteIndex] = frame.data[byteIndex];
  }
  statistics_.uniqueIdCount++;
}

void CanService::queueDiagnosticFrame(const CanFrameSnapshot& frame) {
  if (frame.extended || frame.remote || frame.id < 0x7E8 || frame.id > 0x7EF) {
    return;
  }

  if (diagnosticQueueCount_ >= AppConfig::CAN_DIAGNOSTIC_QUEUE_SIZE) {
    statistics_.diagnosticQueueOverflows++;
    diagnosticQueueTail_ = (diagnosticQueueTail_ + 1) % AppConfig::CAN_DIAGNOSTIC_QUEUE_SIZE;
    diagnosticQueueCount_--;
  }

  diagnosticQueue_[diagnosticQueueHead_] = frame;
  diagnosticQueueHead_ = (diagnosticQueueHead_ + 1) % AppConfig::CAN_DIAGNOSTIC_QUEUE_SIZE;
  diagnosticQueueCount_++;
}

bool CanService::popDiagnosticFrame(CanFrameSnapshot& frame) {
  if (diagnosticQueueCount_ == 0) {
    return false;
  }

  frame = diagnosticQueue_[diagnosticQueueTail_];
  diagnosticQueueTail_ = (diagnosticQueueTail_ + 1) % AppConfig::CAN_DIAGNOSTIC_QUEUE_SIZE;
  diagnosticQueueCount_--;
  return true;
}

void CanService::updateRateCounter() {
  const uint32_t now = millis();
  const uint32_t elapsed = now - rateWindowStartMs_;

  if (elapsed >= 1000) {
    statistics_.framesPerSecond = elapsed > 0
        ? static_cast<uint32_t>((framesInWindow_ * 1000ULL) / elapsed)
        : 0;

    if (statistics_.framesPerSecond > statistics_.peakFramesPerSecond) {
      statistics_.peakFramesPerSecond = statistics_.framesPerSecond;
    }

    framesInWindow_ = 0;
    rateWindowStartMs_ = now;
  }
}

void CanService::printFrameToSerial(
    const CanFrameSnapshot& frame,
    bool transmitted) const {
  Serial.print(transmitted ? F("TX ") : F("RX "));
  Serial.print(frame.extended ? F("EXT 0x") : F("STD 0x"));
  Serial.print(frame.id, HEX);
  Serial.print(F(" DLC:"));
  Serial.print(frame.dlc);
  Serial.print(F(" DATA:"));

  if (frame.remote) {
    Serial.print(F(" RTR"));
  } else {
    for (uint8_t index = 0; index < frame.dlc; ++index) {
      Serial.print(' ');
      if (frame.data[index] < 0x10) Serial.print('0');
      Serial.print(frame.data[index], HEX);
    }
  }
  Serial.println();
}

void CanService::clearStatistics() {
  statistics_ = CanStatistics{};
  lastFrame_ = CanFrameSnapshot{};
  lastTransmittedFrame_ = CanFrameSnapshot{};

  for (uint8_t index = 0; index < AppConfig::CAN_MAX_TRACKED_IDS; ++index) {
    idTable_[index] = CanIdEntry{};
  }

  diagnosticQueueHead_ = 0;
  diagnosticQueueTail_ = 0;
  diagnosticQueueCount_ = 0;
  framesInWindow_ = 0;
  rateWindowStartMs_ = millis();
}

void CanService::setSerialStreaming(bool enabled) {
  serialStreaming_ = enabled;
}

bool CanService::initialized() const { return initialized_; }

bool CanService::busActive() const {
  return lastFrame_.valid &&
      millis() - lastFrame_.timestampMs <= AppConfig::CAN_ACTIVE_TIMEOUT_MS;
}

bool CanService::canTransmit() const {
  return initialized_ && operatingMode_ == CanOperatingMode::Normal;
}

uint8_t CanService::initializationResult() const { return initializationResult_; }
uint8_t CanService::controllerError() const { return controllerError_; }
uint8_t CanService::lastTransmitResult() const { return lastTransmitResult_; }
CanBitrate CanService::bitrate() const { return bitrate_; }
CanOperatingMode CanService::operatingMode() const { return operatingMode_; }
const CanFrameSnapshot& CanService::lastFrame() const { return lastFrame_; }
const CanFrameSnapshot& CanService::lastTransmittedFrame() const { return lastTransmittedFrame_; }
const CanStatistics& CanService::statistics() const { return statistics_; }

const CanIdEntry* CanService::idEntry(uint8_t index) const {
  if (index >= statistics_.uniqueIdCount) return nullptr;
  return &idTable_[index];
}
