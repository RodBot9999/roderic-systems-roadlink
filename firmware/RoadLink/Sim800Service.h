#pragma once

#include <Arduino.h>
#include "GpsService.h"
#include "ObdService.h"

enum class Sim800State : uint8_t {
  Disabled,
  ResetAsserted,
  BootWait,
  Synchronizing,
  EchoOff,
  SimReady,
  Registration,
  SignalQuality,
  GprsAttach,
  BearerType,
  BearerApn,
  BearerUser,
  BearerPassword,
  BearerOpen,
  BearerQuery,
  Ready,
  HttpTerminateBefore,
  HttpInitialize,
  HttpCid,
  HttpUrl,
  HttpContent,
  HttpDataPrompt,
  HttpDataResult,
  HttpAction,
  HttpTerminateAfter,
  RetryWait
};

struct Sim800Snapshot {
  bool enabled = false;
  bool simReady = false;
  bool networkRegistered = false;
  bool gprsAttached = false;
  bool bearerOpen = false;
  bool modemResponsive = false;
  bool sending = false;
  uint8_t signalQuality = 99;
  int16_t lastHttpStatus = 0;
  uint32_t successfulPosts = 0;
  uint32_t failedPosts = 0;
  uint32_t resetCount = 0;
  uint32_t lastPostMs = 0;
  String operatorName;
  String ipAddress;
  String lastError;
};

class Sim800Service {
public:
  Sim800Service(GpsService& gps, ObdService& obd, uint8_t uartNumber);

  void begin(
      uint32_t baud,
      int8_t rxPin,
      int8_t txPin,
      uint8_t resetPin,
      bool enabled,
      bool autoSend,
      bool sendGps,
      bool sendObd,
      uint32_t sendIntervalMs,
      const uint8_t serverIp[4],
      uint16_t serverPort,
      uint32_t accessKey);
  void update();

  void setEnabled(bool enabled);
  bool enabled() const;
  void setAutoSend(bool enabled);
  bool autoSend() const;
  void setSendInterval(uint32_t intervalMs);
  uint32_t sendInterval() const;
  void setPayloadSelection(bool sendGps, bool sendObd);
  void setEndpoint(const uint8_t serverIp[4], uint16_t serverPort, uint32_t accessKey);
  bool endpointConfigured() const;
  String endpointLabel() const;

  bool requestSendNow();
  void requestReset();
  bool ready() const;
  Sim800State state() const;
  const char* stateLabel() const;
  const Sim800Snapshot& snapshot() const;
  const String& lastPayload() const;

private:
  void readSerial();
  void enterState(Sim800State state);
  void startCommand(const String& command, uint32_t timeoutMs);
  bool commandOk() const;
  bool commandError() const;
  bool timedOut() const;
  bool stepCommand(
      const String& command,
      Sim800State next,
      uint32_t timeoutMs,
      bool acceptError = false);
  void startResetSequence();
  void scheduleRetry(Sim800State target, const String& reason);
  void finishPost(bool success, int16_t httpStatus, const String& reason);
  void updateStateMachine();
  void parseSignalQuality();
  void parseBearer();
  int16_t parseHttpStatus() const;
  bool registrationConfirmed() const;
  String buildPayload() const;
  String jsonString(const String& value) const;
  String numberOrNull(bool valid, float value, uint8_t decimals) const;

  GpsService& gps_;
  ObdService& obd_;
  HardwareSerial serial_;

  uint8_t resetPin_ = 255;
  bool serialStarted_ = false;
  bool autoSend_ = false;
  bool sendGps_ = true;
  bool sendObd_ = true;
  bool sendRequested_ = false;
  bool resetRequested_ = false;
  uint32_t sendIntervalMs_ = 10000;
  uint8_t serverIp_[4] = {0, 0, 0, 0};
  uint16_t serverPort_ = 0;
  uint32_t accessKey_ = 0;

  Sim800State state_ = Sim800State::Disabled;
  Sim800State retryTarget_ = Sim800State::Synchronizing;
  uint32_t stateStartedMs_ = 0;
  uint32_t deadlineMs_ = 0;
  bool commandIssued_ = false;
  String response_;
  String payload_;
  Sim800Snapshot snapshot_;
};
