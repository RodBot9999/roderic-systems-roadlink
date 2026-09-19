#pragma once

#include <Arduino.h>
#include "GpsService.h"
#include "ObdService.h"

enum class A7670State : uint8_t {
  Disabled,
  BootWait,
  UartDrain,
  Synchronizing,
  EchoOff,
  ErrorMode,
  RegistrationMode,
  SimReady,
  RadioQuery,
  RadioOnline,
  OperatorQuery,
  OperatorAuto,
  Registration,
  RegistrationWait,
  SignalQuality,
  PacketAttachQuery,
  PacketAttach,
  PdpStatus,
  PdpContextQuery,
  PdpContext,
  PdpAuth,
  PdpQuery,
  Ready,
  HttpTerminateBefore,
  HttpInitialize,
  HttpUrl,
  HttpContent,
  HttpDataPrompt,
  HttpDataResult,
  HttpAction,
  HttpRead,
  HttpTerminateAfter,
  RetryWait,
  ModemRestart,
  Restarted
};

struct RemoteStreamingConfig {
  bool valid = false;
  uint32_t revision = 0;
  bool running = false;
  bool sendGps = true;
  bool sendObd = true;
  uint16_t obdFieldMask = StreamField::DEFAULT_MASK;
  uint32_t intervalSeconds = 10;
};

struct A7670Snapshot {
  bool enabled = false;
  bool simReady = false;
  bool networkRegistered = false;
  bool packetAttached = false;
  bool pdpActive = false;
  bool modemResponsive = false;
  bool sending = false;
  uint8_t signalQuality = 99;
  int8_t registrationStatus = -1;
  int8_t radioFunctionality = -1;
  int16_t lastHttpStatus = 0;
  uint32_t successfulPosts = 0;
  uint32_t failedPosts = 0;
  uint32_t successfulHeartbeats = 0;
  uint32_t failedHeartbeats = 0;
  uint32_t lastHeartbeatMs = 0;
  uint32_t reconnectCount = 0;
  uint32_t lastPostMs = 0;
  uint32_t failureCount = 0;
  uint32_t lastFailureMs = 0;
  String ipAddress;
  String operatorName;
  String apn;
  String lastError;
  String lastFailureReason;
  String lastFailureType;
  String lastFailureStage;
  String lastFailedCommand;
  String lastFailureResponse;
  String lastRetryTarget;
};

class A7670Service {
public:
  A7670Service(GpsService& gps, ObdService& obd, uint8_t uartNumber);

  void begin(
      uint32_t baud,
      int8_t rxPin,
      int8_t txPin,
      bool enabled,
      bool sendGps,
      bool sendObd,
      uint32_t sendIntervalMs,
      const uint8_t serverIp[4],
      uint16_t serverPort,
      uint32_t accessKey);
  void update();

  void setEnabled(bool enabled);
  bool enabled() const;
  bool startTelemetry();
  void stopTelemetry();
  bool telemetryRunning() const;
  void setSendInterval(uint32_t intervalMs);
  uint32_t sendInterval() const;
  void setPayloadSelection(bool sendGps, bool sendObd);
  void setConfigSnapshot(uint32_t revision, uint16_t obdFieldMask);
  bool takeRemoteConfig(RemoteStreamingConfig& config);
  void setEndpoint(const uint8_t serverIp[4], uint16_t serverPort, uint32_t accessKey);
  bool endpointConfigured() const;
  String endpointLabel() const;

  bool requestSendNow();
  void requestReconnect();
  void requestSoftwareRestart();
  bool softwareRestartFinished() const;
  bool softwareRestartSucceeded() const;
  bool ready() const;
  A7670State state() const;
  const char* stateLabel() const;
  const A7670Snapshot& snapshot() const;
  const String& lastPayload() const;

private:
  void readSerial();
  void enterState(A7670State state);
  void startCommand(const String& command, uint32_t timeoutMs);
  bool commandOk() const;
  bool commandError() const;
  bool timedOut() const;
  bool stepCommand(
      const String& command,
      A7670State next,
      uint32_t timeoutMs,
      bool acceptError = false);
  void startInitializationSequence();
  bool settlePendingCommand();
  bool isHttpState() const;
  void scheduleRetry(A7670State target, const String& reason);
  void finishPost(bool success, int16_t httpStatus, const String& reason);
  void recordFailure(
      const String& reason,
      const String& type,
      const String& command = "",
      const String& response = "",
      A7670State retryTarget = A7670State::Disabled);
  String detectedFailureType() const;
  const char* stateLabelFor(A7670State state) const;
  void updateStateMachine();
  void parseSignalQuality();
  void parsePdpAddress();
  int16_t parseHttpStatus() const;
  void parsePdpContext();
  void parseOperator();
  void parseRemoteConfig();
  int16_t parseHttpBodyLength() const;
  bool httpReadComplete() const;
  bool jsonUnsigned(const String& json, const char* key, uint32_t& value) const;
  bool jsonBoolean(const String& json, const char* key, bool& value) const;
  bool configuredApnAvailable() const;
  String buildPayload() const;
  String buildHeartbeatPayload() const;
  void appendConfigJson(String& json) const;
  void appendObdNumber(
      String& json,
      bool& first,
      const char* key,
      bool valid,
      float value,
      uint8_t decimals) const;
  String jsonString(const String& value) const;
  String gpsNumberOrNull(const String& value, uint8_t decimals, bool integer = false) const;
  String numberOrNull(bool valid, float value, uint8_t decimals) const;

  enum class HttpRequestKind : uint8_t { Telemetry, Heartbeat };

  GpsService& gps_;
  ObdService& obd_;
  HardwareSerial serial_;

  bool serialStarted_ = false;
  bool telemetryRunning_ = false;
  bool sendGps_ = true;
  bool sendObd_ = true;
  bool sendRequested_ = false;
  bool reconnectRequested_ = false;
  bool stopRequested_ = false;
  bool restartRequested_ = false;
  bool restartFinished_ = false;
  bool restartSucceeded_ = false;
  bool httpCleanupNeeded_ = false;
  bool heartbeatRequested_ = true;
  bool contextActive_ = false;
  bool registrationRecoveryAttempted_ = false;
  uint32_t registrationStartedMs_ = 0;
  uint32_t lastSerialByteMs_ = 0;
  uint32_t lastSendAttemptMs_ = 0;
  uint32_t lastStatusCheckMs_ = 0;
  uint32_t lastHeartbeatAttemptMs_ = 0;
  uint32_t sendIntervalMs_ = 10000;
  uint8_t serverIp_[4] = {0, 0, 0, 0};
  uint16_t serverPort_ = 0;
  uint32_t accessKey_ = 0;
  uint32_t configRevision_ = 1;
  uint16_t configObdFieldMask_ = StreamField::DEFAULT_MASK;
  int16_t currentHttpStatus_ = 0;
  int16_t currentHttpBodyLength_ = 0;
  HttpRequestKind httpRequestKind_ = HttpRequestKind::Telemetry;
  RemoteStreamingConfig remoteConfig_;

  A7670State state_ = A7670State::Disabled;
  A7670State retryTarget_ = A7670State::Synchronizing;
  uint32_t stateStartedMs_ = 0;
  uint32_t deadlineMs_ = 0;
  bool commandIssued_ = false;
  String response_;
  String currentCommand_;
  String payload_;
  A7670Snapshot snapshot_;
};
