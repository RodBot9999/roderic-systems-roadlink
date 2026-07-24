#pragma once

#include <Arduino.h>
#include <mcp_can.h>
#include "AppConfig.h"
#include "AppState.h"

struct CanFrameSnapshot {
  bool valid = false;
  bool extended = false;
  bool remote = false;
  uint32_t id = 0;
  uint8_t dlc = 0;
  uint8_t data[8] = {};
  uint32_t timestampMs = 0;
};

struct CanIdEntry {
  bool used = false;
  bool extended = false;
  uint32_t id = 0;
  uint32_t count = 0;
  uint32_t lastSeenMs = 0;
  uint8_t dlc = 0;
  uint8_t lastData[8] = {};
};

struct CanStatistics {
  uint32_t totalFrames = 0;
  uint32_t standardFrames = 0;
  uint32_t extendedFrames = 0;
  uint32_t remoteFrames = 0;
  uint32_t framesPerSecond = 0;
  uint32_t peakFramesPerSecond = 0;
  uint32_t tableOverflowCount = 0;
  uint8_t uniqueIdCount = 0;

  uint32_t transmittedFrames = 0;
  uint32_t transmitErrors = 0;
  uint32_t diagnosticQueueOverflows = 0;
};

class CanService {
public:
  CanService(uint8_t chipSelectPin, uint8_t interruptPin);

  bool begin(CanBitrate bitrate, CanOperatingMode mode);
  bool reinitialize(CanBitrate bitrate, CanOperatingMode mode);
  bool setOperatingMode(CanOperatingMode mode);
  void update();

  bool sendFrame(
      uint32_t id,
      bool extended,
      uint8_t dlc,
      const uint8_t* data,
      bool remote = false);

  bool popDiagnosticFrame(CanFrameSnapshot& frame);

  void clearStatistics();
  void setSerialStreaming(bool enabled);

  bool initialized() const;
  bool busActive() const;
  bool canTransmit() const;
  uint8_t initializationResult() const;
  uint8_t controllerError() const;
  uint8_t lastTransmitResult() const;
  CanBitrate bitrate() const;
  CanOperatingMode operatingMode() const;

  const CanFrameSnapshot& lastFrame() const;
  const CanFrameSnapshot& lastTransmittedFrame() const;
  const CanStatistics& statistics() const;
  const CanIdEntry* idEntry(uint8_t index) const;

private:
  byte libraryBitrate(CanBitrate bitrate) const;
  byte libraryMode(CanOperatingMode mode) const;
  void processFrame(uint32_t rawId, uint8_t dlc, const uint8_t* data);
  void updateRateCounter();
  void trackIdentifier(const CanFrameSnapshot& frame);
  void queueDiagnosticFrame(const CanFrameSnapshot& frame);
  void printFrameToSerial(const CanFrameSnapshot& frame, bool transmitted) const;

  uint8_t chipSelectPin_;
  uint8_t interruptPin_;
  MCP_CAN controller_;

  bool initialized_ = false;
  bool serialStreaming_ = false;
  uint8_t initializationResult_ = 0xFF;
  uint8_t controllerError_ = 0;
  uint8_t lastTransmitResult_ = 0xFF;
  CanBitrate bitrate_ = CanBitrate::K500;
  CanOperatingMode operatingMode_ = CanOperatingMode::ListenOnly;

  CanFrameSnapshot lastFrame_;
  CanFrameSnapshot lastTransmittedFrame_;
  CanStatistics statistics_;
  CanIdEntry idTable_[AppConfig::CAN_MAX_TRACKED_IDS] = {};

  CanFrameSnapshot diagnosticQueue_[AppConfig::CAN_DIAGNOSTIC_QUEUE_SIZE] = {};
  uint8_t diagnosticQueueHead_ = 0;
  uint8_t diagnosticQueueTail_ = 0;
  uint8_t diagnosticQueueCount_ = 0;

  uint32_t rateWindowStartMs_ = 0;
  uint32_t framesInWindow_ = 0;
  uint32_t lastErrorPollMs_ = 0;
};
