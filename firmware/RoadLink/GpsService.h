#pragma once

#include <Arduino.h>

struct GpsSnapshot {
  String utcTime;
  String date;
  String latitudeRaw;
  String latitudeDirection;
  String longitudeRaw;
  String longitudeDirection;
  String fixQuality;
  String rmcStatus;
  String satellites;
  String hdop;
  String altitudeMeters;
  String speedKnots;
  String courseDegrees;

  double latitudeDecimal = 0.0;
  double longitudeDecimal = 0.0;
  bool positionValid = false;
};

struct GpsStatistics {
  uint32_t bytesReceived = 0;
  uint32_t sentenceCount = 0;
  uint32_t validChecksumCount = 0;
  uint32_t checksumErrorCount = 0;
  uint32_t ggaCount = 0;
  uint32_t rmcCount = 0;
  uint32_t ppsCount = 0;
  uint32_t lastByteMs = 0;
  uint32_t lastSentenceMs = 0;
  uint32_t lastPpsMs = 0;
};

class GpsService {
public:
  explicit GpsService(uint8_t uartNumber);

  void begin(uint32_t baud, int8_t rxPin, int8_t txPin, uint8_t ppsPin);
  void update();
  void resetStatistics();
  void setRawSerialEnabled(bool enabled);
  bool rawSerialEnabled() const;

  const GpsSnapshot& snapshot() const;
  const GpsStatistics& statistics() const;
  const String& lastSentence() const;
  const String& lastSentenceType() const;

  bool hasFix() const;
  uint32_t lastByteAgeMs() const;
  uint32_t lastSentenceAgeMs() const;

private:
  bool checksumValid(const String& sentence) const;
  String field(const String& sentence, int index) const;
  void processSentence(const String& sentence);
  void parseGga(const String& sentence);
  void parseRmc(const String& sentence);
  void updatePps();
  double nmeaToDecimal(const String& raw, const String& direction) const;

  HardwareSerial serial_;
  uint8_t ppsPin_ = 255;
  bool lastPpsState_ = LOW;
  bool rawSerialEnabled_ = false;

  char sentenceBuffer_[160] = {};
  uint8_t sentenceIndex_ = 0;

  GpsSnapshot snapshot_;
  GpsStatistics statistics_;
  String lastSentence_;
  String lastSentenceType_ = "NONE";
};
