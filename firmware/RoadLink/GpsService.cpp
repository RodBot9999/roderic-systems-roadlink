#include "GpsService.h"

GpsService::GpsService(uint8_t uartNumber)
  : serial_(uartNumber) {}

void GpsService::begin(uint32_t baud, int8_t rxPin, int8_t txPin, uint8_t ppsPin) {
  ppsPin_ = ppsPin;
  pinMode(ppsPin_, INPUT);
  lastPpsState_ = digitalRead(ppsPin_);

  serial_.begin(baud, SERIAL_8N1, rxPin, txPin);
}

void GpsService::update() {
  updatePps();

  while (serial_.available()) {
    const char character = static_cast<char>(serial_.read());

    statistics_.bytesReceived++;
    statistics_.lastByteMs = millis();

    if (rawSerialEnabled_) {
      Serial.write(character);
    }

    if (character == '\n') {
      sentenceBuffer_[sentenceIndex_] = '\0';
      sentenceIndex_ = 0;

      String sentence(sentenceBuffer_);
      sentence.trim();

      if (sentence.startsWith("$")) {
        processSentence(sentence);
      }
    } else if (character != '\r') {
      if (sentenceIndex_ < sizeof(sentenceBuffer_) - 1) {
        sentenceBuffer_[sentenceIndex_++] = character;
      } else {
        sentenceIndex_ = 0;
      }
    }
  }
}

void GpsService::updatePps() {
  const bool currentState = digitalRead(ppsPin_);

  if (currentState == HIGH && lastPpsState_ == LOW) {
    statistics_.ppsCount++;
    statistics_.lastPpsMs = millis();
  }

  lastPpsState_ = currentState;
}

bool GpsService::checksumValid(const String& sentence) const {
  const int starIndex = sentence.indexOf('*');

  if (starIndex < 0 || starIndex + 2 >= sentence.length()) {
    return false;
  }

  uint8_t calculated = 0;
  for (int index = 1; index < starIndex; ++index) {
    calculated ^= static_cast<uint8_t>(sentence[index]);
  }

  const String suppliedHex = sentence.substring(starIndex + 1, starIndex + 3);
  const uint8_t supplied = static_cast<uint8_t>(strtoul(suppliedHex.c_str(), nullptr, 16));
  return calculated == supplied;
}

String GpsService::field(const String& sentence, int index) const {
  int currentField = 0;
  int startIndex = 0;

  for (int characterIndex = 0; characterIndex <= sentence.length(); ++characterIndex) {
    if (characterIndex == sentence.length() ||
        sentence[characterIndex] == ',' ||
        sentence[characterIndex] == '*') {
      if (currentField == index) {
        return sentence.substring(startIndex, characterIndex);
      }

      currentField++;
      startIndex = characterIndex + 1;
    }
  }

  return "";
}

void GpsService::processSentence(const String& sentence) {
  statistics_.sentenceCount++;
  statistics_.lastSentenceMs = millis();
  lastSentence_ = sentence;

  const int commaIndex = sentence.indexOf(',');
  lastSentenceType_ = commaIndex > 1
      ? sentence.substring(1, commaIndex)
      : "NMEA";

  if (!checksumValid(sentence)) {
    statistics_.checksumErrorCount++;
    return;
  }

  statistics_.validChecksumCount++;

  if (sentence.startsWith("$GPGGA") || sentence.startsWith("$GNGGA")) {
    parseGga(sentence);
  } else if (sentence.startsWith("$GPRMC") || sentence.startsWith("$GNRMC")) {
    parseRmc(sentence);
  }
}

void GpsService::parseGga(const String& sentence) {
  snapshot_.utcTime = field(sentence, 1);
  snapshot_.latitudeRaw = field(sentence, 2);
  snapshot_.latitudeDirection = field(sentence, 3);
  snapshot_.longitudeRaw = field(sentence, 4);
  snapshot_.longitudeDirection = field(sentence, 5);
  snapshot_.fixQuality = field(sentence, 6);
  snapshot_.satellites = field(sentence, 7);
  snapshot_.hdop = field(sentence, 8);
  snapshot_.altitudeMeters = field(sentence, 9);

  snapshot_.latitudeDecimal = nmeaToDecimal(
      snapshot_.latitudeRaw,
      snapshot_.latitudeDirection);
  snapshot_.longitudeDecimal = nmeaToDecimal(
      snapshot_.longitudeRaw,
      snapshot_.longitudeDirection);

  snapshot_.positionValid =
      snapshot_.fixQuality.length() > 0 &&
      snapshot_.fixQuality != "0" &&
      snapshot_.latitudeRaw.length() > 0 &&
      snapshot_.longitudeRaw.length() > 0;

  statistics_.ggaCount++;
}

void GpsService::parseRmc(const String& sentence) {
  snapshot_.utcTime = field(sentence, 1);
  snapshot_.rmcStatus = field(sentence, 2);
  snapshot_.latitudeRaw = field(sentence, 3);
  snapshot_.latitudeDirection = field(sentence, 4);
  snapshot_.longitudeRaw = field(sentence, 5);
  snapshot_.longitudeDirection = field(sentence, 6);
  snapshot_.speedKnots = field(sentence, 7);
  snapshot_.courseDegrees = field(sentence, 8);
  snapshot_.date = field(sentence, 9);

  snapshot_.latitudeDecimal = nmeaToDecimal(
      snapshot_.latitudeRaw,
      snapshot_.latitudeDirection);
  snapshot_.longitudeDecimal = nmeaToDecimal(
      snapshot_.longitudeRaw,
      snapshot_.longitudeDirection);

  if (snapshot_.rmcStatus == "A") {
    snapshot_.positionValid = true;
  }

  statistics_.rmcCount++;
}

double GpsService::nmeaToDecimal(const String& raw, const String& direction) const {
  if (raw.length() == 0) {
    return 0.0;
  }

  const double rawValue = raw.toDouble();
  const int degrees = static_cast<int>(rawValue / 100.0);
  const double minutes = rawValue - degrees * 100.0;
  double decimal = degrees + minutes / 60.0;

  if (direction == "S" || direction == "W") {
    decimal = -decimal;
  }

  return decimal;
}

void GpsService::resetStatistics() {
  statistics_ = GpsStatistics{};
  lastSentence_ = "";
  lastSentenceType_ = "NONE";
}

void GpsService::setRawSerialEnabled(bool enabled) {
  rawSerialEnabled_ = enabled;
}

bool GpsService::rawSerialEnabled() const {
  return rawSerialEnabled_;
}

const GpsSnapshot& GpsService::snapshot() const {
  return snapshot_;
}

const GpsStatistics& GpsService::statistics() const {
  return statistics_;
}

const String& GpsService::lastSentence() const {
  return lastSentence_;
}

const String& GpsService::lastSentenceType() const {
  return lastSentenceType_;
}

bool GpsService::hasFix() const {
  return snapshot_.positionValid;
}

uint32_t GpsService::lastByteAgeMs() const {
  return statistics_.lastByteMs == 0
      ? UINT32_MAX
      : millis() - statistics_.lastByteMs;
}

uint32_t GpsService::lastSentenceAgeMs() const {
  return statistics_.lastSentenceMs == 0
      ? UINT32_MAX
      : millis() - statistics_.lastSentenceMs;
}
