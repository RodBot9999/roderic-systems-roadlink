#include "A7670Service.h"

#include "AppConfig.h"
#include "ModemReply.h"

A7670Service::A7670Service(
    GpsService& gps,
    ObdService& obd,
    uint8_t uartNumber)
  : gps_(gps),
    obd_(obd),
    serial_(uartNumber) {}

void A7670Service::begin(
    uint32_t baud,
    int8_t rxPin,
    int8_t txPin,
    bool enabled,
    bool sendGps,
    bool sendObd,
    uint32_t sendIntervalMs,
    const uint8_t serverIp[4],
    uint16_t serverPort,
    uint32_t accessKey) {
  telemetryRunning_ = false;
  setPayloadSelection(sendGps, sendObd);
  setSendInterval(sendIntervalMs);
  setEndpoint(serverIp, serverPort, accessKey);

  // UART idle is HIGH. Keep RX from floating and generating an interrupt storm
  // when the modem or its TX lead is disconnected.
  pinMode(rxPin, INPUT_PULLUP);
  serial_.begin(baud, SERIAL_8N1, rxPin, txPin);
  serialStarted_ = true;

  snapshot_ = A7670Snapshot{};
  lastSerialByteMs_ = millis();
  snapshot_.enabled = enabled;
  if (enabled) startInitializationSequence();
  else enterState(A7670State::Disabled);
}

void A7670Service::update() {
  if (!serialStarted_) return;
  readSerial();

  if (restartFinished_) return;
  if (state_ != A7670State::ModemRestart &&
      (!snapshot_.enabled || reconnectRequested_ || stopRequested_ || restartRequested_)) {
    // Do not interrupt a command or leave the modem in HTTPDATA input mode.
    if (!settlePendingCommand()) return;
    stopRequested_ = false;
    if (restartRequested_) {
      enterState(A7670State::ModemRestart);
    } else if (!snapshot_.enabled) {
      reconnectRequested_ = false;
      if (state_ != A7670State::Disabled) enterState(A7670State::Disabled);
      return;
    } else {
      reconnectRequested_ = false;
      startInitializationSequence();
    }
  }

  updateStateMachine();
}

void A7670Service::setEnabled(bool enabled) {
  if (snapshot_.enabled == enabled) return;
  snapshot_.enabled = enabled;
  stopTelemetry();
  // This pauses software only. The external module remains powered/registered.
  reconnectRequested_ = enabled;
}

bool A7670Service::enabled() const {
  return snapshot_.enabled;
}

bool A7670Service::startTelemetry() {
  if (!snapshot_.enabled || !ready() || !endpointConfigured()) {
    snapshot_.lastError = !snapshot_.enabled ? "Enable modem checks first"
        : (!endpointConfigured() ? "Configure receiver IP and port before START"
                                 : "Wait for modem status checks before START");
    return false;
  }
  telemetryRunning_ = true;
  sendRequested_ = true;
  snapshot_.lastError = "";
  return true;
}

void A7670Service::stopTelemetry() {
  telemetryRunning_ = false;
  sendRequested_ = false;
  stopRequested_ = isHttpState() || httpCleanupNeeded_;
}

bool A7670Service::telemetryRunning() const {
  return telemetryRunning_;
}

void A7670Service::setSendInterval(uint32_t intervalMs) {
  if (intervalMs < AppConfig::SIM_SEND_INTERVAL_MIN_MS)
    sendIntervalMs_ = AppConfig::SIM_SEND_INTERVAL_MIN_MS;
  else if (intervalMs > AppConfig::SIM_SEND_INTERVAL_MAX_MS)
    sendIntervalMs_ = AppConfig::SIM_SEND_INTERVAL_MAX_MS;
  else
    sendIntervalMs_ = intervalMs;
}

uint32_t A7670Service::sendInterval() const {
  return sendIntervalMs_;
}

void A7670Service::setPayloadSelection(bool sendGps, bool sendObd) {
  sendGps_ = sendGps;
  sendObd_ = sendObd;
}

void A7670Service::setEndpoint(
    const uint8_t serverIp[4],
    uint16_t serverPort,
    uint32_t accessKey) {
  memcpy(serverIp_, serverIp, sizeof(serverIp_));
  serverPort_ = serverPort;
  accessKey_ = accessKey % 1000000UL;
  heartbeatRequested_ = true;
}

bool A7670Service::endpointConfigured() const {
  return serverPort_ != 0 &&
      (serverIp_[0] || serverIp_[1] || serverIp_[2] || serverIp_[3]);
}

String A7670Service::endpointLabel() const {
  String value;
  for (uint8_t index = 0; index < 4; ++index) {
    if (index) value += '.';
    value += serverIp_[index];
  }
  value += ':';
  value += serverPort_;
  return value;
}

bool A7670Service::requestSendNow() {
  if (!snapshot_.enabled || !telemetryRunning_ || !ready()) return false;
  if (!endpointConfigured()) {
    snapshot_.lastError = "Configure receiver IP and port";
    recordFailure(
        snapshot_.lastError,
        "SETTINGS",
        "",
        "Receiver endpoint is incomplete",
        state_);
    return false;
  }
  sendRequested_ = true;
  return true;
}

void A7670Service::requestReconnect() {
  stopTelemetry();
  if (snapshot_.enabled) reconnectRequested_ = true;
}

void A7670Service::setConfigSnapshot(
    uint32_t revision,
    uint16_t obdFieldMask) {
  configRevision_ = revision ? revision : 1;
  configObdFieldMask_ = obdFieldMask & StreamField::ALL;
  heartbeatRequested_ = true;
}

bool A7670Service::takeRemoteConfig(RemoteStreamingConfig& config) {
  if (!remoteConfig_.valid) return false;
  config = remoteConfig_;
  remoteConfig_.valid = false;
  return true;
}

void A7670Service::requestSoftwareRestart() {
  stopTelemetry();
  restartRequested_ = true;
  restartFinished_ = false;
  restartSucceeded_ = false;
}

bool A7670Service::softwareRestartFinished() const { return restartFinished_; }
bool A7670Service::softwareRestartSucceeded() const { return restartSucceeded_; }

bool A7670Service::isHttpState() const {
  return state_ >= A7670State::HttpTerminateBefore &&
      state_ <= A7670State::HttpTerminateAfter;
}

bool A7670Service::ready() const {
  return state_ == A7670State::Ready && snapshot_.simReady &&
      snapshot_.networkRegistered && snapshot_.packetAttached;
}

A7670State A7670Service::state() const {
  return state_;
}

const char* A7670Service::stateLabel() const {
  if (commandIssued_ && state_ != A7670State::ModemRestart &&
      (!snapshot_.enabled || reconnectRequested_ || stopRequested_ || restartRequested_)) {
    return "Finishing pending AT";
  }
  return stateLabelFor(state_);
}

const char* A7670Service::stateLabelFor(A7670State state) const {
  switch (state) {
    case A7670State::Disabled:            return "Disabled";
    case A7670State::BootWait:            return "Waiting for modem";
    case A7670State::UartDrain:           return "Clearing stale UART";
    case A7670State::Synchronizing:       return "Synchronizing";
    case A7670State::EchoOff:             return "Configuring UART";
    case A7670State::ErrorMode:           return "Enabling AT diagnostics";
    case A7670State::RegistrationMode:    return "Configuring LTE queries";
    case A7670State::SimReady:            return "Checking SIM";
    case A7670State::RadioQuery:          return "Checking radio status";
    case A7670State::RadioOnline:         return "Enabling radio";
    case A7670State::OperatorQuery:       return "Checking operator";
    case A7670State::OperatorAuto:        return "Selecting LTE operator";
    case A7670State::Registration:        return "Registering LTE";
    case A7670State::RegistrationWait:    return "Waiting for LTE";
    case A7670State::SignalQuality:       return "Reading signal";
    case A7670State::PacketAttachQuery:   return "Checking packet attach";
    case A7670State::PacketAttach:        return "Attaching packet data";
    case A7670State::PdpStatus:           return "Checking PDP status";
    case A7670State::PdpContextQuery:     return "Reading existing APN";
    case A7670State::PdpContext:
    case A7670State::PdpAuth:
    case A7670State::PdpQuery:            return "Checking LTE data";
    case A7670State::Ready:               return "Ready";
    case A7670State::HttpTerminateBefore:
    case A7670State::HttpInitialize:
    case A7670State::HttpUrl:
    case A7670State::HttpContent:
    case A7670State::HttpDataPrompt:
    case A7670State::HttpDataResult:
    case A7670State::HttpAction:
    case A7670State::HttpRead:
    case A7670State::HttpTerminateAfter:  return "Sending HTTP";
    case A7670State::RetryWait:           return "Retry wait";
    case A7670State::ModemRestart:        return "Restarting modem UART";
    case A7670State::Restarted:           return "Restarting ESP32";
  }
  return "Unknown";
}

const A7670Snapshot& A7670Service::snapshot() const {
  return snapshot_;
}

const String& A7670Service::lastPayload() const {
  return payload_;
}

void A7670Service::readSerial() {
  while (serial_.available()) {
    const char character = static_cast<char>(serial_.read());
    lastSerialByteMs_ = millis();
    if (response_.length() >= 2048) {
      response_.remove(0, 512);
    }
    response_ += character;
  }
}

void A7670Service::enterState(A7670State state) {
  state_ = state;
  stateStartedMs_ = millis();
  deadlineMs_ = 0;
  commandIssued_ = false;
  response_ = "";
}

void A7670Service::startCommand(const String& command, uint32_t timeoutMs) {
  response_ = "";
  currentCommand_ = command;
  if (command == "AT+HTTPINIT") httpCleanupNeeded_ = true;
  serial_.print(command);
  serial_.print('\r');
  commandIssued_ = true;
  deadlineMs_ = millis() + timeoutMs;
}

bool A7670Service::commandOk() const {
  return response_.indexOf("\r\nOK\r\n") >= 0 ||
      response_.startsWith("OK\r\n") ||
      response_.endsWith("\r\nOK\r\n");
}

bool A7670Service::commandError() const {
  return response_.indexOf("\r\nERROR\r\n") >= 0 ||
      response_.startsWith("ERROR\r\n") ||
      response_.indexOf("+CME ERROR:") >= 0;
}

bool A7670Service::timedOut() const {
  return commandIssued_ &&
      static_cast<int32_t>(millis() - deadlineMs_) >= 0;
}

bool A7670Service::stepCommand(
    const String& command,
    A7670State next,
    uint32_t timeoutMs,
    bool acceptError) {
  if (!commandIssued_) {
    startCommand(command, timeoutMs);
    return false;
  }

  if (commandOk() || (acceptError && commandError())) {
    if (command == "AT+HTTPTERM") httpCleanupNeeded_ = false;
    enterState(next);
    return true;
  }

  if (commandError() || timedOut()) {
    if (isHttpState() && command != "AT+HTTPTERM") {
      finishPost(false, 0, String("Command failed: ") + command);
      enterState(A7670State::HttpTerminateAfter);
    } else {
      if (command == "AT+HTTPTERM") httpCleanupNeeded_ = false;
      scheduleRetry(A7670State::UartDrain, String("Command failed: ") + command);
    }
  }
  return false;
}

void A7670Service::startInitializationSequence() {
  sendRequested_ = false;
  snapshot_.modemResponsive = false;
  snapshot_.simReady = false;
  snapshot_.networkRegistered = false;
  snapshot_.packetAttached = false;
  snapshot_.pdpActive = false;
  snapshot_.sending = false;
  snapshot_.ipAddress = "";
  snapshot_.registrationStatus = -1;
  snapshot_.radioFunctionality = -1;
  snapshot_.operatorName = "";
  snapshot_.apn = "";
  contextActive_ = false;
  registrationRecoveryAttempted_ = false;
  registrationStartedMs_ = millis();
  snapshot_.lastError = "";
  snapshot_.reconnectCount++;
  enterState(A7670State::BootWait);
}

bool A7670Service::settlePendingCommand() {
  if (commandIssued_) {
    if (state_ == A7670State::HttpDataPrompt &&
        response_.indexOf("DOWNLOAD") >= 0) {
      // Complete local data input, but never issue HTTPACTION after STOP.
      response_ = "";
      serial_.print(payload_);
      deadlineMs_ = millis() + 15000;
      state_ = A7670State::HttpDataResult;
      return false;
    }
    const bool httpResult = state_ == A7670State::HttpAction;
    const bool complete = httpResult
        ? parseHttpStatus() > 0
        : (state_ == A7670State::HttpRead ? httpReadComplete() : commandOk());
    if (!complete && !commandError() && !timedOut()) return false;
    if (httpResult) {
      const int16_t status = parseHttpStatus();
      finishPost(status >= 200 && status < 300, status,
          status ? String("HTTP status ") + status : "Stopped POST did not complete");
    }
    if (currentCommand_ == "AT+HTTPTERM") httpCleanupNeeded_ = false;
    commandIssued_ = false;
    response_ = "";
  }
  if (httpCleanupNeeded_) {
    enterState(A7670State::HttpTerminateAfter);
    startCommand("AT+HTTPTERM", AppConfig::MODEM_HTTP_SERVICE_TIMEOUT_MS);
    return false;
  }
  snapshot_.sending = false;
  return true;
}

void A7670Service::scheduleRetry(
    A7670State target,
    const String& reason) {
  retryTarget_ = target;
  snapshot_.lastError = reason;
  snapshot_.sending = false;
  recordFailure(
      reason,
      detectedFailureType(),
      currentCommand_,
      response_,
      target);
  enterState(A7670State::RetryWait);
}

void A7670Service::finishPost(
    bool success,
    int16_t httpStatus,
    const String& reason) {
  snapshot_.lastHttpStatus = httpStatus;
  const uint32_t now = millis();
  snapshot_.sending = false;

  if (httpRequestKind_ == HttpRequestKind::Heartbeat) {
    snapshot_.lastHeartbeatMs = now;
    if (success) snapshot_.successfulHeartbeats++;
    else snapshot_.failedHeartbeats++;
  } else {
    snapshot_.lastPostMs = now;
    if (success) snapshot_.successfulPosts++;
    else snapshot_.failedPosts++;
  }

  if (success) {
    snapshot_.lastError = "";
  } else {
    snapshot_.lastError = reason;
    recordFailure(
        reason,
        httpStatus ? "HTTP STATUS" : detectedFailureType(),
        currentCommand_,
        response_,
        A7670State::Ready);
  }
}

void A7670Service::recordFailure(
    const String& reason,
    const String& type,
    const String& command,
    const String& response,
    A7670State retryTarget) {
  const uint32_t now = millis();
  const bool duplicate = snapshot_.lastFailureReason == reason &&
      snapshot_.lastFailedCommand == command &&
      now - snapshot_.lastFailureMs < 1000;
  if (duplicate) return;

  snapshot_.failureCount++;
  snapshot_.lastFailureMs = now;
  snapshot_.lastFailureReason = reason;
  snapshot_.lastFailureType = type.length() ? type : "STATE CHECK";
  snapshot_.lastFailureStage = stateLabelFor(state_);

  if (command.startsWith("AT+CGAUTH=")) {
    snapshot_.lastFailedCommand = "AT+CGAUTH=<credentials hidden>";
    snapshot_.lastFailureResponse = "APN credentials hidden";
  } else {
    snapshot_.lastFailedCommand = command;
    snapshot_.lastFailureResponse = response;
  }
  if (snapshot_.lastFailureResponse.length() > 240) {
    snapshot_.lastFailureResponse.remove(240);
  }
  snapshot_.lastRetryTarget = stateLabelFor(retryTarget);
}

String A7670Service::detectedFailureType() const {
  if (commandError()) return "MODEM ERROR";
  if (timedOut()) return response_.length() ? "TIMEOUT" : "NO RESPONSE / TIMEOUT";
  return "STATE CHECK";
}

void A7670Service::updateStateMachine() {
  if (!snapshot_.enabled && state_ != A7670State::ModemRestart) return;
  const uint32_t now = millis();
  switch (state_) {
    case A7670State::Disabled:
      startInitializationSequence();
      break;
    case A7670State::BootWait:
      if (now - stateStartedMs_ >= AppConfig::MODEM_BOOT_WAIT_MS)
        enterState(A7670State::UartDrain);
      break;
    case A7670State::UartDrain:
      if (now - lastSerialByteMs_ >= AppConfig::MODEM_UART_QUIET_MS ||
          now - stateStartedMs_ >= 5000)
        enterState(A7670State::Synchronizing);
      break;
    case A7670State::Synchronizing:
      if (!commandIssued_) startCommand("AT", AppConfig::SIM_COMMAND_TIMEOUT_MS);
      else if (commandOk()) {
        snapshot_.modemResponsive = true;
        enterState(A7670State::EchoOff);
      } else if (commandError() || timedOut()) {
        snapshot_.modemResponsive = false;
        scheduleRetry(A7670State::UartDrain, "A7670SA did not respond to AT");
      }
      break;
    case A7670State::EchoOff:
      stepCommand("ATE0", A7670State::ErrorMode, AppConfig::SIM_COMMAND_TIMEOUT_MS);
      break;
    case A7670State::ErrorMode:
      stepCommand("AT+CMEE=2", A7670State::RegistrationMode, AppConfig::SIM_COMMAND_TIMEOUT_MS);
      break;
    case A7670State::RegistrationMode:
      stepCommand("AT+CEREG=0", A7670State::SimReady, AppConfig::SIM_COMMAND_TIMEOUT_MS);
      break;
    case A7670State::SimReady:
      if (!commandIssued_) startCommand("AT+CPIN?", AppConfig::SIM_COMMAND_TIMEOUT_MS);
      else if (commandOk()) {
        snapshot_.simReady = response_.indexOf("+CPIN: READY") >= 0;
        if (snapshot_.simReady) enterState(A7670State::RadioQuery);
        else scheduleRetry(A7670State::SimReady, "SIM card is not ready");
      } else if (commandError() || timedOut())
        scheduleRetry(A7670State::SimReady, "SIM query failed");
      break;
    case A7670State::RadioQuery:
      if (!commandIssued_) startCommand("AT+CFUN?", AppConfig::SIM_COMMAND_TIMEOUT_MS);
      else if (commandOk()) {
        snapshot_.radioFunctionality = ModemReply::scalar(response_.c_str(), "+CFUN:");
        if (snapshot_.radioFunctionality < 0)
          scheduleRetry(A7670State::RadioQuery, "Invalid radio status reply");
        else enterState(snapshot_.radioFunctionality == 1
            ? A7670State::OperatorQuery : A7670State::RadioOnline);
      } else if (commandError() || timedOut())
        scheduleRetry(A7670State::RadioQuery, "Radio status query failed");
      break;
    case A7670State::RadioOnline:
      if (stepCommand("AT+CFUN=1", A7670State::OperatorQuery, 10000))
        snapshot_.radioFunctionality = 1;
      break;
    case A7670State::OperatorQuery:
      if (!commandIssued_) startCommand("AT+COPS?", AppConfig::SIM_COMMAND_TIMEOUT_MS);
      else if (commandOk()) {
        parseOperator();
        const int mode = ModemReply::first(response_.c_str(), "+COPS:");
        if (mode < 0 || mode > 4) {
          scheduleRetry(A7670State::OperatorQuery, "Invalid operator status reply");
        } else if (mode == 2 && !registrationRecoveryAttempted_) {
          registrationRecoveryAttempted_ = true;
          enterState(A7670State::OperatorAuto);
        } else enterState(A7670State::Registration);
      } else if (commandError() || timedOut())
        scheduleRetry(A7670State::OperatorQuery, "Operator status query failed");
      break;
    case A7670State::OperatorAuto:
      if (stepCommand("AT+COPS=0", A7670State::Registration, 180000)) {
        registrationStartedMs_ = now;
        registrationRecoveryAttempted_ = true;
      }
      break;
    case A7670State::Registration:
      if (!commandIssued_) startCommand("AT+CEREG?", AppConfig::SIM_COMMAND_TIMEOUT_MS);
      else if (commandOk()) {
        const int previousStatus = snapshot_.registrationStatus;
        snapshot_.registrationStatus = ModemReply::registration(response_.c_str());
        const int status = snapshot_.registrationStatus;
        snapshot_.networkRegistered = status == 1 || status == 5;
        if (snapshot_.networkRegistered) {
          registrationRecoveryAttempted_ = false;
          snapshot_.lastError = "";
          enterState(A7670State::SignalQuality);
        } else if (status < 0) {
          scheduleRetry(A7670State::Registration, "Invalid LTE registration reply");
        } else {
          snapshot_.packetAttached = false;
          snapshot_.pdpActive = false;
          snapshot_.ipAddress = "";
          if (status == 3 && previousStatus != 3) {
            snapshot_.lastError = "LTE registration denied by network";
            recordFailure(snapshot_.lastError, "LTE REGISTRATION", currentCommand_,
                response_, A7670State::Registration);
          }
          // Searching is a normal modem state, not a failed AT command.
          if (now - registrationStartedMs_ >= AppConfig::MODEM_REGISTRATION_WAIT_MS) {
            snapshot_.lastError = status == 3 ? "LTE registration denied by network"
                : String("LTE registration timeout; status ") + status;
            recordFailure(snapshot_.lastError, "LTE REGISTRATION", currentCommand_,
                response_, A7670State::Registration);
            registrationStartedMs_ = now;
            if (!registrationRecoveryAttempted_) {
              registrationRecoveryAttempted_ = true;
              enterState(A7670State::OperatorAuto);
              break;
            }
          }
          enterState(A7670State::RegistrationWait);
        }
      } else if (commandError() || timedOut())
        scheduleRetry(A7670State::Registration, "Registration query failed");
      break;
    case A7670State::RegistrationWait:
      if (now - stateStartedMs_ >= AppConfig::SIM_RETRY_DELAY_MS)
        enterState(A7670State::Registration);
      break;
    case A7670State::SignalQuality:
      if (!commandIssued_) startCommand("AT+CSQ", AppConfig::SIM_COMMAND_TIMEOUT_MS);
      else if (commandOk()) {
        parseSignalQuality();
        enterState(A7670State::PacketAttachQuery);
      } else if (commandError() || timedOut())
        scheduleRetry(A7670State::SignalQuality, "Signal query failed");
      break;
    case A7670State::PacketAttachQuery:
      if (!commandIssued_) startCommand("AT+CGATT?", AppConfig::SIM_COMMAND_TIMEOUT_MS);
      else if (commandOk()) {
        const int attached = ModemReply::scalar(response_.c_str(), "+CGATT:");
        snapshot_.packetAttached = attached == 1;
        if (attached < 0 || attached > 1)
          scheduleRetry(A7670State::PacketAttachQuery, "Invalid packet attach reply");
        else enterState(attached == 1 ? A7670State::PdpStatus : A7670State::PacketAttach);
      } else if (commandError() || timedOut())
        scheduleRetry(A7670State::PacketAttachQuery, "Packet attach query failed");
      break;
    case A7670State::PacketAttach:
      stepCommand("AT+CGATT=1", A7670State::PacketAttachQuery, 75000);
      break;
    case A7670State::PdpStatus:
      if (!commandIssued_) startCommand("AT+CGACT?", AppConfig::SIM_COMMAND_TIMEOUT_MS);
      else if (commandOk()) {
        contextActive_ = ModemReply::pair(response_.c_str(), "+CGACT:", 1) == 1;
        enterState(A7670State::PdpContextQuery);
      } else if (commandError() || timedOut())
        scheduleRetry(A7670State::PdpStatus, "PDP status query failed");
      break;
    case A7670State::PdpContextQuery:
      if (!commandIssued_) startCommand("AT+CGDCONT?", AppConfig::SIM_COMMAND_TIMEOUT_MS);
      else if (commandOk()) {
        parsePdpContext();
        // Preserve the already-active LTE default bearer on a warm enable.
        // Never write the placeholder APN into a modem provisioned by the SIM.
        enterState(!contextActive_ && configuredApnAvailable() &&
            snapshot_.apn != AppConfig::SIM_APN ? A7670State::PdpContext
            : (!contextActive_ && configuredApnAvailable() &&
                (strlen(AppConfig::SIM_APN_USER) || strlen(AppConfig::SIM_APN_PASSWORD))
                ? A7670State::PdpAuth : A7670State::PdpQuery));
      } else if (commandError() || timedOut())
        scheduleRetry(A7670State::PdpContextQuery, "APN query failed");
      break;
    case A7670State::PdpContext:
      if (stepCommand(String("AT+CGDCONT=1,\"IP\",\"") + AppConfig::SIM_APN + "\"",
          (strlen(AppConfig::SIM_APN_USER) || strlen(AppConfig::SIM_APN_PASSWORD))
              ? A7670State::PdpAuth : A7670State::PdpQuery,
          AppConfig::SIM_COMMAND_TIMEOUT_MS)) snapshot_.apn = AppConfig::SIM_APN;
      break;
    case A7670State::PdpAuth:
      stepCommand(String("AT+CGAUTH=1,1,\"") + AppConfig::SIM_APN_PASSWORD +
          "\",\"" + AppConfig::SIM_APN_USER + "\"", A7670State::PdpQuery,
          AppConfig::SIM_COMMAND_TIMEOUT_MS);
      break;
    case A7670State::PdpQuery:
      if (!commandIssued_) startCommand("AT+CGPADDR=1", AppConfig::SIM_COMMAND_TIMEOUT_MS);
      else if (commandOk()) {
        parsePdpAddress();
        snapshot_.lastError = "";
        lastStatusCheckMs_ = now;
        // HTTPINIT may establish data later for telemetry or heartbeat.
        // Do not force CGACT=1,1 on an LTE default bearer.
        enterState(A7670State::Ready);
      } else if (commandError() || timedOut())
        scheduleRetry(A7670State::PdpStatus, "PDP address query failed");
      break;
    case A7670State::Ready:
      if (now - lastStatusCheckMs_ >= AppConfig::MODEM_STATUS_REFRESH_MS) {
        registrationStartedMs_ = now;
        enterState(A7670State::RadioQuery);
      } else if (telemetryRunning_ &&
          now - lastHeartbeatAttemptMs_ < AppConfig::REMOTE_HEARTBEAT_INTERVAL_MS &&
          (sendRequested_ || now - lastSendAttemptMs_ >= sendIntervalMs_)) {
        sendRequested_ = false;
        if (!endpointConfigured()) {
          stopTelemetry();
          snapshot_.lastError = "Configure receiver IP and port before START";
          break;
        }
        lastSendAttemptMs_ = now;
        httpRequestKind_ = HttpRequestKind::Telemetry;
        payload_ = buildPayload();
        snapshot_.sending = true;
        enterState(A7670State::HttpTerminateBefore);
      } else if (endpointConfigured() && (heartbeatRequested_ ||
          now - lastHeartbeatAttemptMs_ >= AppConfig::REMOTE_HEARTBEAT_INTERVAL_MS)) {
        heartbeatRequested_ = false;
        lastHeartbeatAttemptMs_ = now;
        httpRequestKind_ = HttpRequestKind::Heartbeat;
        payload_ = buildHeartbeatPayload();
        snapshot_.sending = true;
        enterState(A7670State::HttpTerminateBefore);
      }
      break;
    case A7670State::HttpTerminateBefore:
      stepCommand("AT+HTTPTERM", A7670State::HttpInitialize,
          AppConfig::MODEM_HTTP_SERVICE_TIMEOUT_MS, true);
      break;
    case A7670State::HttpInitialize:
      stepCommand("AT+HTTPINIT", A7670State::HttpUrl, AppConfig::MODEM_HTTP_SERVICE_TIMEOUT_MS);
      break;
    case A7670State::HttpUrl:
      stepCommand(String("AT+HTTPPARA=\"URL\",\"http://") + endpointLabel() +
          (httpRequestKind_ == HttpRequestKind::Heartbeat
              ? "/heartbeat\"" : "/telemetry\""),
          A7670State::HttpContent,
          AppConfig::SIM_COMMAND_TIMEOUT_MS);
      break;
    case A7670State::HttpContent:
      stepCommand("AT+HTTPPARA=\"CONTENT\",\"application/json\"",
          A7670State::HttpDataPrompt, AppConfig::SIM_COMMAND_TIMEOUT_MS);
      break;
    case A7670State::HttpDataPrompt:
      if (!commandIssued_) startCommand(
          String("AT+HTTPDATA=") + payload_.length() + ",10000", 12000);
      else if (response_.indexOf("DOWNLOAD") >= 0) {
        response_ = "";
        serial_.print(payload_);
        deadlineMs_ = now + 15000;
        state_ = A7670State::HttpDataResult;
      } else if (commandError() || timedOut()) {
        finishPost(false, 0, "HTTP data prompt failed");
        enterState(A7670State::HttpTerminateAfter);
      }
      break;
    case A7670State::HttpDataResult:
      if (commandOk()) enterState(A7670State::HttpAction);
      else if (commandError() || timedOut()) {
        finishPost(false, 0, "HTTP payload upload failed");
        enterState(A7670State::HttpTerminateAfter);
      }
      break;
    case A7670State::HttpAction:
      if (!commandIssued_) startCommand("AT+HTTPACTION=1", AppConfig::SIM_HTTP_TIMEOUT_MS);
      else if (parseHttpStatus() > 0) {
        currentHttpStatus_ = parseHttpStatus();
        currentHttpBodyLength_ = parseHttpBodyLength();
        if (currentHttpStatus_ >= 200 && currentHttpStatus_ < 300 &&
            currentHttpBodyLength_ > 0) {
          enterState(A7670State::HttpRead);
        } else {
          finishPost(
              currentHttpStatus_ >= 200 && currentHttpStatus_ < 300,
              currentHttpStatus_,
              String("HTTP status ") + currentHttpStatus_);
          enterState(A7670State::HttpTerminateAfter);
        }
      } else if (commandError() || timedOut()) {
        finishPost(false, 0, "HTTP POST failed or timed out");
        enterState(A7670State::HttpTerminateAfter);
      }
      break;
    case A7670State::HttpRead:
      if (!commandIssued_) {
        const int16_t readLength = currentHttpBodyLength_ > 1536 ? 1536 : currentHttpBodyLength_;
        startCommand(String("AT+HTTPREAD=0,") + readLength,
            AppConfig::MODEM_HTTP_SERVICE_TIMEOUT_MS);
      } else if (httpReadComplete()) {
        if (currentHttpBodyLength_ <= 1536) parseRemoteConfig();
        finishPost(true, currentHttpStatus_, "HTTP response read");
        enterState(A7670State::HttpTerminateAfter);
      } else if (commandError() || timedOut()) {
        finishPost(false, currentHttpStatus_, "HTTP response read failed");
        enterState(A7670State::HttpTerminateAfter);
      }
      break;
    case A7670State::HttpTerminateAfter:
      if (stepCommand("AT+HTTPTERM", A7670State::Ready,
          AppConfig::MODEM_HTTP_SERVICE_TIMEOUT_MS, true)) snapshot_.sending = false;
      break;
    case A7670State::RetryWait:
      if (now - stateStartedMs_ >= AppConfig::SIM_RETRY_DELAY_MS)
        enterState(retryTarget_);
      break;
    case A7670State::ModemRestart:
      if (!commandIssued_) startCommand("AT+CRESET", 10000);
      else if (commandOk() || commandError() || timedOut()) {
        restartSucceeded_ = commandOk();
        restartFinished_ = true;
        enterState(A7670State::Restarted);
      }
      break;
    case A7670State::Restarted:
      break;
  }
}

void A7670Service::parseSignalQuality() {
  const int marker = response_.indexOf("+CSQ:");
  if (marker < 0) return;
  const int comma = response_.indexOf(',', marker);
  if (comma < 0) return;
  snapshot_.signalQuality = static_cast<uint8_t>(
      response_.substring(marker + 5, comma).toInt());
}

void A7670Service::parsePdpAddress() {
  snapshot_.ipAddress = "";
  snapshot_.pdpActive = false;
  const int marker = response_.indexOf("+CGPADDR:");
  if (marker < 0) return;

  const int comma = response_.indexOf(',', marker);
  if (comma < 0) return;
  const int end = response_.indexOf('\r', comma);
  String address = response_.substring(
      comma + 1,
      end < 0 ? response_.length() : end);
  address.trim();
  address.replace("\"", "");

  snapshot_.ipAddress = address;
  snapshot_.pdpActive = address.length() > 0 && address != "0.0.0.0";
}

int16_t A7670Service::parseHttpStatus() const {
  if (parseHttpBodyLength() < 0) return 0;
  const int status = ModemReply::pair(response_.c_str(), "+HTTPACTION:", 1);
  return status > 0 ? status : 0;
}

bool A7670Service::configuredApnAvailable() const {
  return strlen(AppConfig::SIM_APN) &&
      strcmp(AppConfig::SIM_APN, "YOUR_CARRIER_APN") != 0;
}

int16_t A7670Service::parseHttpBodyLength() const {
  const char* cursor = strstr(response_.c_str(), "+HTTPACTION:");
  if (!cursor) return -1;
  cursor += strlen("+HTTPACTION:");
  long method = 0, status = 0, length = 0;
  if (!ModemReply::number(cursor, method) || *cursor++ != ',' ||
      !ModemReply::number(cursor, status) || *cursor++ != ',' ||
      !ModemReply::number(cursor, length) ||
      (*cursor != '\r' && *cursor != '\n') || method != 1) return -1;
  return length >= 0 && length <= INT16_MAX ? static_cast<int16_t>(length) : 0;
}

bool A7670Service::httpReadComplete() const {
  // A76XX sends OK before the body, followed by +HTTPREAD: 0 after it.
  // Wait for the declared bytes and trailer, not the initial acknowledgment.
  const int marker = response_.indexOf("+HTTPREAD:");
  if (marker < 0) return false;
  const int lineEnd = response_.indexOf('\n', marker);
  // first() returns the last matching line; parse only the leading header.
  const String header = lineEnd >= 0 ? response_.substring(marker, lineEnd + 1) : "";
  const int bodyLength = ModemReply::first(header.c_str(), "+HTTPREAD:");
  if (lineEnd < 0 || bodyLength < 0 || bodyLength > 1536 ||
      response_.length() < static_cast<unsigned int>(lineEnd + 1 + bodyLength)) return false;
  const String trailer = response_.substring(lineEnd + 1 + bodyLength);
  return trailer.indexOf("\r\n+HTTPREAD: 0\r\n") >= 0 ||
      trailer.indexOf("\r\nOK\r\n") >= 0;
}

bool A7670Service::jsonUnsigned(
    const String& json,
    const char* key,
    uint32_t& value) const {
  const String marker = String('"') + key + '"';
  const int keyIndex = json.indexOf(marker);
  if (keyIndex < 0) return false;
  int cursor = json.indexOf(':', keyIndex + marker.length());
  if (cursor < 0) return false;
  cursor++;
  while (cursor < json.length() && isspace(static_cast<unsigned char>(json[cursor]))) cursor++;
  if (cursor >= json.length() || json[cursor] < '0' || json[cursor] > '9') return false;
  uint32_t parsed = 0;
  while (cursor < json.length() && json[cursor] >= '0' && json[cursor] <= '9') {
    const uint8_t digit = static_cast<uint8_t>(json[cursor] - '0');
    if (parsed > (UINT32_MAX - digit) / 10UL) return false;
    parsed = parsed * 10UL + digit;
    cursor++;
  }
  while (cursor < json.length() && isspace(static_cast<unsigned char>(json[cursor]))) cursor++;
  if (cursor >= json.length() || (json[cursor] != ',' && json[cursor] != '}')) return false;
  value = parsed;
  return true;
}

bool A7670Service::jsonBoolean(
    const String& json,
    const char* key,
    bool& value) const {
  const String marker = String('"') + key + '"';
  const int keyIndex = json.indexOf(marker);
  if (keyIndex < 0) return false;
  int cursor = json.indexOf(':', keyIndex + marker.length());
  if (cursor < 0) return false;
  cursor++;
  while (cursor < json.length() && isspace(static_cast<unsigned char>(json[cursor]))) cursor++;
  if (json.substring(cursor, cursor + 4) == "true") {
    value = true;
    cursor += 4;
  } else if (json.substring(cursor, cursor + 5) == "false") {
    value = false;
    cursor += 5;
  } else return false;
  while (cursor < json.length() && isspace(static_cast<unsigned char>(json[cursor]))) cursor++;
  return cursor < json.length() && (json[cursor] == ',' || json[cursor] == '}');
}

void A7670Service::parseRemoteConfig() {
  const int start = response_.indexOf('{');
  int end = -1;
  for (int index = response_.length() - 1; index >= 0; --index) {
    if (response_[index] == '}') {
      end = index;
      break;
    }
  }
  if (start < 0 || end <= start) return;
  const String json = response_.substring(start, end + 1);
  RemoteStreamingConfig candidate;
  uint32_t fieldMask = 0;
  if (!jsonUnsigned(json, "revision", candidate.revision) ||
      !jsonBoolean(json, "running", candidate.running) ||
      !jsonBoolean(json, "gps", candidate.sendGps) ||
      !jsonBoolean(json, "obd", candidate.sendObd) ||
      !jsonUnsigned(json, "obd_fields", fieldMask) ||
      !jsonUnsigned(json, "interval_seconds", candidate.intervalSeconds)) {
    return;
  }
  if (candidate.revision == 0 || fieldMask > StreamField::ALL ||
      candidate.intervalSeconds == 0 || candidate.intervalSeconds > 359999UL) {
    return;
  }
  candidate.obdFieldMask = static_cast<uint16_t>(fieldMask);
  candidate.valid = true;
  remoteConfig_ = candidate;
}

void A7670Service::parsePdpContext() {
  int marker = -1;
  while ((marker = response_.indexOf("+CGDCONT:", marker + 1)) >= 0) {
    const int comma = response_.indexOf(',', marker);
    if (comma < 0 || response_.substring(marker + 9, comma).toInt() != 1) continue;
    const int typeStart = response_.indexOf('"', comma);
    const int typeEnd = response_.indexOf('"', typeStart + 1);
    const int apnStart = response_.indexOf('"', typeEnd + 1);
    const int apnEnd = response_.indexOf('"', apnStart + 1);
    if (typeStart >= 0 && typeEnd >= 0 && apnStart >= 0 && apnEnd >= 0)
      snapshot_.apn = response_.substring(apnStart + 1, apnEnd);
    break;
  }
}

void A7670Service::parseOperator() {
  const int marker = response_.indexOf("+COPS:");
  const int firstQuote = response_.indexOf('"', marker);
  const int lastQuote = response_.indexOf('"', firstQuote + 1);
  snapshot_.operatorName = firstQuote >= 0 && lastQuote >= 0
      ? response_.substring(firstQuote + 1, lastQuote) : "";
}

String A7670Service::buildPayload() const {
  const GpsSnapshot& gps = gps_.snapshot();
  const ObdLiveData& obd = obd_.liveData();
  const bool gpsFix = gps_.hasFix();
  const float gpsSpeedKmh = gps.speedKnots.length()
      ? gps.speedKnots.toFloat() * 1.852f
      : 0.0f;

  String json;
  json.reserve(640);
  char key[7];
  snprintf(key, sizeof(key), "%06lu", static_cast<unsigned long>(accessKey_));
  json += "{\"access_key\":\"";
  json += key;
  json += "\",\"device\":\"roadlink\",\"uptime_ms\":";
  json += millis();
  appendConfigJson(json);
  if (sendGps_) {
    json += ",\"gps\":{\"valid\":";
    json += gpsFix ? "true" : "false";
    json += ",\"latitude\":";
    json += gpsFix ? String(gps.latitudeDecimal, 6) : "null";
    json += ",\"longitude\":";
    json += gpsFix ? String(gps.longitudeDecimal, 6) : "null";
    json += ",\"altitude_m\":";
    json += gpsFix ? gpsNumberOrNull(gps.altitudeMeters, 1) : "null";
    json += ",\"speed_kmh\":";
    json += gpsFix && gps.speedKnots.length() ? String(gpsSpeedKmh, 1) : "null";
    json += ",\"course_deg\":";
    json += gpsFix ? gpsNumberOrNull(gps.courseDegrees, 1) : "null";
    json += ",\"satellites\":";
    json += gpsNumberOrNull(gps.satellites, 0, true);
    json += ",\"utc_time\":";
    json += jsonString(gps.utcTime);
    json += ",\"utc_date\":";
    json += jsonString(gps.date);
    json += '}';
  }
  if (sendObd_) {
    json += ",\"obd\":{";
    bool first = true;
    const uint16_t mask = obd_.livePidMask();
    if (mask & StreamField::RPM)
      appendObdNumber(json, first, "rpm", obd.rpmValid, obd.rpm, 0);
    if (mask & StreamField::SPEED)
      appendObdNumber(json, first, "speed_kmh", obd.speedValid, obd.speedKmh, 0);
    if (mask & StreamField::COOLANT)
      appendObdNumber(json, first, "coolant_c", obd.coolantValid, obd.coolantC, 0);
    if (mask & StreamField::THROTTLE)
      appendObdNumber(json, first, "throttle_pct", obd.throttleValid, obd.throttlePercent, 1);
    if (mask & StreamField::MANIFOLD)
      appendObdNumber(json, first, "map_kpa", obd.mapValid, obd.mapKpa, 0);
    if (mask & StreamField::INTAKE_TEMP)
      appendObdNumber(json, first, "intake_c", obd.intakeTempValid, obd.intakeTempC, 0);
    if (mask & StreamField::TIMING)
      appendObdNumber(json, first, "timing_deg", obd.timingValid, obd.timingDegrees, 1);
    if (mask & StreamField::VOLTAGE)
      appendObdNumber(json, first, "voltage_v", obd.voltageValid, obd.moduleVoltage, 2);
    if (mask & StreamField::FUEL_LEVEL)
      appendObdNumber(json, first, "fuel_pct", obd.fuelLevelValid, obd.fuelLevelPercent, 1);
    if (!first) json += ',';
    json += "\"age_ms\":";
    json += obd.lastUpdateMs ? String(millis() - obd.lastUpdateMs) : "null";
    json += '}';
  }
  json += '}';
  return json;
}

String A7670Service::buildHeartbeatPayload() const {
  String json;
  json.reserve(260);
  char key[7];
  snprintf(key, sizeof(key), "%06lu", static_cast<unsigned long>(accessKey_));
  json += "{\"access_key\":\"";
  json += key;
  json += "\",\"device\":\"roadlink\",\"type\":\"heartbeat\",\"uptime_ms\":";
  json += millis();
  appendConfigJson(json);
  json += '}';
  return json;
}

void A7670Service::appendConfigJson(String& json) const {
  json += ",\"config\":{\"revision\":";
  json += configRevision_;
  json += ",\"running\":";
  json += telemetryRunning_ ? "true" : "false";
  json += ",\"gps\":";
  json += sendGps_ ? "true" : "false";
  json += ",\"obd\":";
  json += sendObd_ ? "true" : "false";
  json += ",\"obd_fields\":";
  json += configObdFieldMask_;
  json += ",\"interval_seconds\":";
  json += sendIntervalMs_ / 1000UL;
  json += '}';
}

void A7670Service::appendObdNumber(
    String& json,
    bool& first,
    const char* key,
    bool valid,
    float value,
    uint8_t decimals) const {
  if (!first) json += ',';
  first = false;
  json += '"';
  json += key;
  json += "\":";
  json += numberOrNull(valid, value, decimals);
}

String A7670Service::jsonString(const String& value) const {
  String escaped = "\"";
  for (uint16_t index = 0; index < value.length(); ++index) {
    const char character = value[index];
    if (character == '"' || character == '\\') escaped += '\\';
    if (static_cast<uint8_t>(character) >= 32) escaped += character;
  }
  escaped += '"';
  return escaped;
}

String A7670Service::gpsNumberOrNull(
    const String& value,
    uint8_t decimals,
    bool integer) const {
  if (!value.length()) return "null";
  bool digit = false;
  bool decimalPoint = false;
  for (uint16_t index = 0; index < value.length(); ++index) {
    const char character = value[index];
    if (character >= '0' && character <= '9') {
      digit = true;
      continue;
    }
    if ((character == '-' || character == '+') && index == 0) continue;
    if (character == '.' && !decimalPoint && !integer) {
      decimalPoint = true;
      continue;
    }
    return "null";
  }
  if (!digit) return "null";
  return integer
      ? String(static_cast<unsigned long>(value.toInt()))
      : String(value.toFloat(), static_cast<unsigned int>(decimals));
}

String A7670Service::numberOrNull(
    bool valid,
    float value,
    uint8_t decimals) const {
  return valid
      ? String(value, static_cast<unsigned int>(decimals))
      : String("null");
}
