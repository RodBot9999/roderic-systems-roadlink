#pragma once

#include <Arduino.h>

enum class InputEvent : uint8_t {
  None,
  RotateLeft,
  RotateRight,
  Press,
  Back
};

class EncoderInput {
public:
  EncoderInput(uint8_t clkPin, uint8_t dtPin, uint8_t swPin);

  void begin();
  InputEvent poll();

private:
  uint8_t clkPin_;
  uint8_t dtPin_;
  uint8_t swPin_;

  uint8_t lastQuadratureState_ = 0;
  int8_t movementAccumulator_ = 0;

  bool lastButtonReading_ = HIGH;
  bool stableButtonState_ = HIGH;
  uint32_t lastButtonChangeMs_ = 0;
};
