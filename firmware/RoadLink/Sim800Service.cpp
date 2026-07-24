#include "Sim800Service.h"

#include "AppConfig.h"

Sim800Service::Sim800Service(
    GpsService& gps,
    ObdService& obd,
    uint8_t uartNumber)
  : gps_(gps),
    obd_(obd),
    serial_(uartNumber) {}

void Sim800Service::begin(
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
    uint32_t accessKey) {
  resetPin_ = resetPin;
  autoSend_ = autoSend;
  setPayloadSelection(sendGps, sendObd);
  setSendInterval(sendIntervalMs);
  setEndpoint(serverIp, serverPort, accessKey);

  // UART idle is HIGH. Keep RX from floating and generating an interrupt storm
  // when the modem or its TX lead is disconnected.
  pinMode(rxPin, INPUT_PULLUP);
  pinMode(resetPin_, OUTPUT);
  digitalWrite(resetPin_, HIGH);
  serial_.begin(baud, SERIAL_8N1, rxPin, txPin);
  serialStarted_ = true;

  snapshot_ = Sim800Snapshot{};
  snapshot_.enabled = enabled;
  if (enabled) startResetSequence();
  else enterState(Sim800State::Disabled);
}

void Sim800Service::update() {
  if (!serialStarted_) return;
  readSerial();

  if (resetRequested_) {
    resetRequested_ = false;
    if (snapshot_.enabled) startResetSequence();
  }

  updateStateMachine();
}

void Sim800Service::setEnabled(bool enabled) {
  if (snapshot_.enabled == enabled) return;
  snapshot_.enabled = enabled;
  sendRequested_ = false;

  if (enabled) {
    startResetSequence();
  } else {
    snapshot_.modemResponsive = false;
    snapshot_.sending = false;
    snapshot_.networkRegistered = false;
    snapshot_.gprsAttached = false;
    snapshot_.bearerOpen = false;
    enterState(Sim800State::Disabled);
  }
}

bool Sim800Service::enabled() const {
  return snapshot_.enabled;
}

void Sim800Service::setAutoSend(bool enabled) {
  autoSend_ = enabled;
}

bool Sim800Service::autoSend() const {
  return autoSend_;
}

void Sim800Service::setSendInterval(uint32_t intervalMs) {
  sendIntervalMs_ = intervalMs < AppConfig::SIM_SEND_INTERVAL_MIN_MS
      ? AppConfig::SIM_SEND_INTERVAL_MIN_MS
      : intervalMs;
}

uint32_t Sim800Service::sendInterval() const {
  return sendIntervalMs_;
}

void Sim800Service::setPayloadSelection(bool sendGps, bool sendObd) {
  sendGps_ = sendGps;
  sendObd_ = sendObd;
}

void Sim800Service::setEndpoint(
    const uint8_t serverIp[4],
    uint16_t serverPort,
    uint32_t accessKey) {
  memcpy(serverIp_, serverIp, sizeof(serverIp_));
  serverPort_ = serverPort;
  accessKey_ = accessKey % 1000000UL;
}

bool Sim800Service::endpointConfigured() const {
  return serverPort_ != 0 &&
      (serverIp_[0] || serverIp_[1] || serverIp_[2] || serverIp_[3]);
}

String Sim800Service::endpointLabel() const {
  String value;
  for (uint8_t index = 0; index < 4; ++index) {
    if (index) value += '.';
    value += serverIp_[index];
  }
  value += ':';
  value += serverPort_;
  return value;
}

bool Sim800Service::requestSendNow() {
  if (!snapshot_.enabled) return false;
  if (!endpointConfigured()) {
    snapshot_.lastError = "Configure receiver IP and port";
    return false;
  }
  sendRequested_ = true;
  return true;
}

void Sim800Service::requestReset() {
  if (snapshot_.enabled) resetRequested_ = true;
}

bool Sim800Service::ready() const {
  return state_ == Sim800State::Ready && snapshot_.bearerOpen;
}

Sim800State Sim800Service::state() const {
  return state_;
}

const char* Sim800Service::stateLabel() const {
  switch (state_) {
    case Sim800State::Disabled:            return "Disabled";
    case Sim800State::ResetAsserted:       return "Resetting";
    case Sim800State::BootWait:            return "Booting";
    case Sim800State::Synchronizing:       return "Synchronizing";
    case Sim800State::EchoOff:             return "Configuring UART";
    case Sim800State::SimReady:            return "Checking SIM";
    case Sim800State::Registration:        return "Registering";
    case Sim800State::SignalQuality:       return "Reading signal";
    case Sim800State::GprsAttach:          return "Attaching GPRS";
    case Sim800State::BearerType:
    case Sim800State::BearerApn:
    case Sim800State::BearerUser:
    case Sim800State::BearerPassword:
    case Sim800State::BearerOpen:
    case Sim800State::BearerQuery:         return "Opening bearer";
    case Sim800State::Ready:               return "Ready";
    case Sim800State::HttpTerminateBefore:
    case Sim800State::HttpInitialize:
    case Sim800State::HttpCid:
    case Sim800State::HttpUrl:
    case Sim800State::HttpContent:
    case Sim800State::HttpDataPrompt:
    case Sim800State::HttpDataResult:
    case Sim800State::HttpAction:
    case Sim800State::HttpTerminateAfter:  return "Sending HTTP";
    case Sim800State::RetryWait:           return "Retry wait";
  }
  return "Unknown";
}

const Sim800Snapshot& Sim800Service::snapshot() const {
  return snapshot_;
}

const String& Sim800Service::lastPayload() const {
  return payload_;
}

void Sim800Service::readSerial() {
  while (serial_.available()) {
    const char character = static_cast<char>(serial_.read());
    if (response_.length() >= 2048) {
      response_.remove(0, 512);
    }
    response_ += character;
  }
}

void Sim800Service::enterState(Sim800State state) {
  state_ = state;
  stateStartedMs_ = millis();
  deadlineMs_ = 0;
  commandIssued_ = false;
  response_ = "";
}

void Sim800Service::startCommand(const String& command, uint32_t timeoutMs) {
  response_ = "";
  serial_.print(command);
  serial_.print('\r');
  commandIssued_ = true;
  deadlineMs_ = millis() + timeoutMs;
}

bool Sim800Service::commandOk() const {
  return response_.indexOf("\r\nOK\r\n") >= 0 ||
      response_.endsWith("\r\nOK\r\n");
}

bool Sim800Service::commandError() const {
  return response_.indexOf("\r\nERROR\r\n") >= 0 ||
      response_.indexOf("+CME ERROR:") >= 0;
}

bool Sim800Service::timedOut() const {
  return commandIssued_ &&
      static_cast<int32_t>(millis() - deadlineMs_) >= 0;
}

bool Sim800Service::stepCommand(
    const String& command,
    Sim800State next,
    uint32_t timeoutMs,
    bool acceptError) {
  if (!commandIssued_) {
    startCommand(command, timeoutMs);
    return false;
  }

  if (commandOk() || (acceptError && commandError())) {
    enterState(next);
    return true;
  }

  if (commandError() || timedOut()) {
    scheduleRetry(
        Sim800State::Synchronizing,
        String("Command failed: ") + command);
  }
  return false;
}

void Sim800Service::startResetSequence() {
  snapshot_.modemResponsive = false;
  snapshot_.simReady = false;
  snapshot_.networkRegistered = false;
  snapshot_.gprsAttached = false;
  snapshot_.bearerOpen = false;
  snapshot_.sending = false;
  snapshot_.ipAddress = "";
  snapshot_.lastError = "";
  snapshot_.resetCount++;
  digitalWrite(resetPin_, LOW);
  enterState(Sim800State::ResetAsserted);
}

void Sim800Service::scheduleRetry(
    Sim800State target,
    const String& reason) {
  retryTarget_ = target;
  snapshot_.lastError = reason;
  snapshot_.sending = false;
  enterState(Sim800State::RetryWait);
}

void Sim800Service::finishPost(
    bool success,
    int16_t httpStatus,
    const String& reason) {
  snapshot_.lastHttpStatus = httpStatus;
  snapshot_.lastPostMs = millis();
  snapshot_.sending = false;

  if (success) {
    snapshot_.successfulPosts++;
    snapshot_.lastError = "";
  } else {
    snapshot_.failedPosts++;
    snapshot_.lastError = reason;
  }
}

void Sim800Service::updateStateMachine() {
  if (!snapshot_.enabled) {
    if (state_ != Sim800State::Disabled) enterState(Sim800State::Disabled);
    return;
  }

  switch (state_) {
    case Sim800State::Disabled:
      startResetSequence();
      break;

    case Sim800State::ResetAsserted:
      if (millis() - stateStartedMs_ >= AppConfig::SIM_RESET_PULSE_MS) {
        digitalWrite(resetPin_, HIGH);
        enterState(Sim800State::BootWait);
      }
      break;

    case Sim800State::BootWait:
      if (millis() - stateStartedMs_ >= AppConfig::SIM_BOOT_WAIT_MS) {
        enterState(Sim800State::Synchronizing);
      }
      break;

    case Sim800State::Synchronizing:
      if (!commandIssued_) {
        startCommand("AT", AppConfig::SIM_COMMAND_TIMEOUT_MS);
      } else if (commandOk()) {
        snapshot_.modemResponsive = true;
        enterState(Sim800State::EchoOff);
      } else if (commandError() || timedOut()) {
        scheduleRetry(
            Sim800State::Synchronizing,
            "SIM800L did not respond to AT");
      }
      break;

    case Sim800State::EchoOff:
      stepCommand("ATE0", Sim800State::SimReady, AppConfig::SIM_COMMAND_TIMEOUT_MS);
      break;

    case Sim800State::SimReady:
      if (!commandIssued_) {
        startCommand("AT+CPIN?", AppConfig::SIM_COMMAND_TIMEOUT_MS);
      } else if (commandOk()) {
        snapshot_.simReady = response_.indexOf("+CPIN: READY") >= 0;
        if (snapshot_.simReady) enterState(Sim800State::Registration);
        else scheduleRetry(Sim800State::SimReady, "SIM card is not ready");
      } else if (commandError() || timedOut()) {
        scheduleRetry(Sim800State::SimReady, "SIM query failed");
      }
      break;

    case Sim800State::Registration:
      if (!commandIssued_) {
        startCommand("AT+CREG?", AppConfig::SIM_COMMAND_TIMEOUT_MS);
      } else if (commandOk()) {
        snapshot_.networkRegistered = registrationConfirmed();
        if (snapshot_.networkRegistered) {
          enterState(Sim800State::SignalQuality);
        } else {
          scheduleRetry(Sim800State::Registration, "Waiting for GSM registration");
        }
      } else if (commandError() || timedOut()) {
        scheduleRetry(Sim800State::Registration, "Registration query failed");
      }
      break;

    case Sim800State::SignalQuality:
      if (!commandIssued_) {
        startCommand("AT+CSQ", AppConfig::SIM_COMMAND_TIMEOUT_MS);
      } else if (commandOk()) {
        parseSignalQuality();
        enterState(Sim800State::GprsAttach);
      } else if (commandError() || timedOut()) {
        scheduleRetry(Sim800State::SignalQuality, "Signal query failed");
      }
      break;

    case Sim800State::GprsAttach:
      if (stepCommand("AT+CGATT=1", Sim800State::BearerType, 75000)) {
        snapshot_.gprsAttached = true;
      }
      break;

    case Sim800State::BearerType:
      stepCommand(
          "AT+SAPBR=3,1,\"Contype\",\"GPRS\"",
          Sim800State::BearerApn,
          AppConfig::SIM_COMMAND_TIMEOUT_MS);
      break;

    case Sim800State::BearerApn:
      stepCommand(
          String("AT+SAPBR=3,1,\"APN\",\"") + AppConfig::SIM_APN + "\"",
          strlen(AppConfig::SIM_APN_USER)
              ? Sim800State::BearerUser
              : (strlen(AppConfig::SIM_APN_PASSWORD)
                  ? Sim800State::BearerPassword
                  : Sim800State::BearerOpen),
          AppConfig::SIM_COMMAND_TIMEOUT_MS);
      break;

    case Sim800State::BearerUser:
      stepCommand(
          String("AT+SAPBR=3,1,\"USER\",\"") + AppConfig::SIM_APN_USER + "\"",
          strlen(AppConfig::SIM_APN_PASSWORD)
              ? Sim800State::BearerPassword
              : Sim800State::BearerOpen,
          AppConfig::SIM_COMMAND_TIMEOUT_MS);
      break;

    case Sim800State::BearerPassword:
      stepCommand(
          String("AT+SAPBR=3,1,\"PWD\",\"") + AppConfig::SIM_APN_PASSWORD + "\"",
          Sim800State::BearerOpen,
          AppConfig::SIM_COMMAND_TIMEOUT_MS);
      break;

    case Sim800State::BearerOpen:
      stepCommand(
          "AT+SAPBR=1,1",
          Sim800State::BearerQuery,
          85000,
          true);
      break;

    case Sim800State::BearerQuery:
      if (!commandIssued_) {
        startCommand("AT+SAPBR=2,1", AppConfig::SIM_COMMAND_TIMEOUT_MS);
      } else if (commandOk()) {
        parseBearer();
        if (snapshot_.bearerOpen) enterState(Sim800State::Ready);
        else scheduleRetry(Sim800State::BearerOpen, "GPRS bearer did not open");
      } else if (commandError() || timedOut()) {
        scheduleRetry(Sim800State::BearerOpen, "Bearer query failed");
      }
      break;

    case Sim800State::Ready:
      if (sendRequested_ ||
          (autoSend_ &&
           (snapshot_.lastPostMs == 0 ||
            millis() - snapshot_.lastPostMs >= sendIntervalMs_))) {
        sendRequested_ = false;
        if (!endpointConfigured()) {
          snapshot_.lastError = "Configure receiver IP and port";
          break;
        }
        payload_ = buildPayload();
        snapshot_.sending = true;
        enterState(Sim800State::HttpTerminateBefore);
      }
      break;

    case Sim800State::HttpTerminateBefore:
      stepCommand(
          "AT+HTTPTERM",
          Sim800State::HttpInitialize,
          AppConfig::SIM_COMMAND_TIMEOUT_MS,
          true);
      break;

    case Sim800State::HttpInitialize:
      stepCommand(
          "AT+HTTPINIT",
          Sim800State::HttpCid,
          AppConfig::SIM_COMMAND_TIMEOUT_MS);
      break;

    case Sim800State::HttpCid:
      stepCommand(
          "AT+HTTPPARA=\"CID\",1",
          Sim800State::HttpUrl,
          AppConfig::SIM_COMMAND_TIMEOUT_MS);
      break;

    case Sim800State::HttpUrl:
      stepCommand(
          String("AT+HTTPPARA=\"URL\",\"http://") + endpointLabel() +
              "/telemetry\"",
          Sim800State::HttpContent,
          AppConfig::SIM_COMMAND_TIMEOUT_MS);
      break;

    case Sim800State::HttpContent:
      stepCommand(
          "AT+HTTPPARA=\"CONTENT\",\"application/json\"",
          Sim800State::HttpDataPrompt,
          AppConfig::SIM_COMMAND_TIMEOUT_MS);
      break;

    case Sim800State::HttpDataPrompt:
      if (!commandIssued_) {
        startCommand(
            String("AT+HTTPDATA=") + payload_.length() + ",10000",
            12000);
      } else if (response_.indexOf("DOWNLOAD") >= 0) {
        response_ = "";
        serial_.print(payload_);
        commandIssued_ = true;
        deadlineMs_ = millis() + 15000;
        state_ = Sim800State::HttpDataResult;
      } else if (commandError() || timedOut()) {
        finishPost(false, 0, "HTTP data prompt failed");
        enterState(Sim800State::HttpTerminateAfter);
      }
      break;

    case Sim800State::HttpDataResult:
      if (commandOk()) {
        enterState(Sim800State::HttpAction);
      } else if (commandError() || timedOut()) {
        finishPost(false, 0, "HTTP payload upload failed");
        enterState(Sim800State::HttpTerminateAfter);
      }
      break;

    case Sim800State::HttpAction:
      if (!commandIssued_) {
        startCommand("AT+HTTPACTION=1", AppConfig::SIM_HTTP_TIMEOUT_MS);
      } else if (response_.indexOf("+HTTPACTION:") >= 0) {
        const int16_t status = parseHttpStatus();
        finishPost(
            status >= 200 && status < 300,
            status,
            String("HTTP status ") + status);
        enterState(Sim800State::HttpTerminateAfter);
      } else if (commandError() || timedOut()) {
        finishPost(false, 0, "HTTP POST timed out");
        enterState(Sim800State::HttpTerminateAfter);
      }
      break;

    case Sim800State::HttpTerminateAfter:
      if (stepCommand(
          "AT+HTTPTERM",
          Sim800State::Ready,
          AppConfig::SIM_COMMAND_TIMEOUT_MS,
          true)) {
        snapshot_.sending = false;
      }
      break;

    case Sim800State::RetryWait:
      if (millis() - stateStartedMs_ >= AppConfig::SIM_RETRY_DELAY_MS) {
        enterState(retryTarget_);
      }
      break;
  }
}

void Sim800Service::parseSignalQuality() {
  const int marker = response_.indexOf("+CSQ:");
  if (marker < 0) return;
  const int comma = response_.indexOf(',', marker);
  if (comma < 0) return;
  snapshot_.signalQuality = static_cast<uint8_t>(
      response_.substring(marker + 5, comma).toInt());
}

void Sim800Service::parseBearer() {
  const int marker = response_.indexOf("+SAPBR:");
  if (marker < 0) return;

  const int firstComma = response_.indexOf(',', marker);
  const int secondComma = response_.indexOf(',', firstComma + 1);
  if (firstComma < 0 || secondComma < 0) return;

  const int status = response_.substring(firstComma + 1, secondComma).toInt();
  snapshot_.bearerOpen = status == 1;

  const int firstQuote = response_.indexOf('"', secondComma);
  const int secondQuote = response_.indexOf('"', firstQuote + 1);
  if (firstQuote >= 0 && secondQuote > firstQuote) {
    snapshot_.ipAddress = response_.substring(firstQuote + 1, secondQuote);
  }
}

int16_t Sim800Service::parseHttpStatus() const {
  const int marker = response_.indexOf("+HTTPACTION:");
  if (marker < 0) return 0;
  const int firstComma = response_.indexOf(',', marker);
  const int secondComma = response_.indexOf(',', firstComma + 1);
  if (firstComma < 0 || secondComma < 0) return 0;
  return static_cast<int16_t>(
      response_.substring(firstComma + 1, secondComma).toInt());
}

bool Sim800Service::registrationConfirmed() const {
  const int marker = response_.indexOf("+CREG:");
  if (marker < 0) return false;
  const int comma = response_.indexOf(',', marker);
  if (comma < 0) return false;
  const int end = response_.indexOf('\r', comma);
  const int status = response_.substring(
      comma + 1,
      end < 0 ? response_.length() : end).toInt();
  return status == 1 || status == 5;
}

String Sim800Service::buildPayload() const {
  const GpsSnapshot& gps = gps_.snapshot();
  const ObdLiveData& obd = obd_.liveData();
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
  if (sendGps_) {
    json += ",\"gps\":{\"valid\":";
    json += gps.positionValid ? "true" : "false";
    json += ",\"latitude\":";
    json += gps.positionValid ? String(gps.latitudeDecimal, 6) : "null";
    json += ",\"longitude\":";
    json += gps.positionValid ? String(gps.longitudeDecimal, 6) : "null";
    json += ",\"altitude_m\":";
    json += gps.altitudeMeters.length() ? gps.altitudeMeters : "null";
    json += ",\"speed_kmh\":";
    json += gps.speedKnots.length() ? String(gpsSpeedKmh, 1) : "null";
    json += ",\"course_deg\":";
    json += gps.courseDegrees.length() ? gps.courseDegrees : "null";
    json += ",\"satellites\":";
    json += gps.satellites.length() ? gps.satellites : "null";
    json += ",\"utc_time\":";
    json += jsonString(gps.utcTime);
    json += ",\"utc_date\":";
    json += jsonString(gps.date);
    json += '}';
  }
  if (sendObd_) {
    json += ",\"obd\":{\"rpm\":";
    json += numberOrNull(obd.rpmValid, obd.rpm, 0);
    json += ",\"speed_kmh\":";
    json += numberOrNull(obd.speedValid, obd.speedKmh, 0);
    json += ",\"coolant_c\":";
    json += numberOrNull(obd.coolantValid, obd.coolantC, 0);
    json += ",\"throttle_pct\":";
    json += numberOrNull(obd.throttleValid, obd.throttlePercent, 1);
    json += ",\"map_kpa\":";
    json += numberOrNull(obd.mapValid, obd.mapKpa, 0);
    json += ",\"intake_c\":";
    json += numberOrNull(obd.intakeTempValid, obd.intakeTempC, 0);
    json += ",\"timing_deg\":";
    json += numberOrNull(obd.timingValid, obd.timingDegrees, 1);
    json += ",\"voltage_v\":";
    json += numberOrNull(obd.voltageValid, obd.moduleVoltage, 2);
    json += ",\"fuel_pct\":";
    json += numberOrNull(obd.fuelLevelValid, obd.fuelLevelPercent, 1);
    json += ",\"age_ms\":";
    json += obd.lastUpdateMs ? String(millis() - obd.lastUpdateMs) : "null";
    json += '}';
  }
  json += '}';
  return json;
}

String Sim800Service::jsonString(const String& value) const {
  String escaped = "\"";
  for (uint16_t index = 0; index < value.length(); ++index) {
    const char character = value[index];
    if (character == '"' || character == '\\') escaped += '\\';
    if (static_cast<uint8_t>(character) >= 32) escaped += character;
  }
  escaped += '"';
  return escaped;
}

String Sim800Service::numberOrNull(
    bool valid,
    float value,
    uint8_t decimals) const {
  return valid
      ? String(value, static_cast<unsigned int>(decimals))
      : String("null");
}
