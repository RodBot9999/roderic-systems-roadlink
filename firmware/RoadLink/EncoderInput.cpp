#include "EncoderInput.h"
#include "AppConfig.h"

EncoderInput::EncoderInput(uint8_t clkPin, uint8_t dtPin, uint8_t swPin)
  : clkPin_(clkPin), dtPin_(dtPin), swPin_(swPin) {}

void EncoderInput::begin() {
  pinMode(clkPin_, INPUT_PULLUP);
  pinMode(dtPin_, INPUT_PULLUP);
  pinMode(swPin_, INPUT_PULLUP);

  lastQuadratureState_ =
      (digitalRead(clkPin_) << 1) |
       digitalRead(dtPin_);
}

InputEvent EncoderInput::poll() {
  static const int8_t transitionTable[16] = {
     0, -1,  1,  0,
     1,  0,  0, -1,
    -1,  0,  0,  1,
     0,  1, -1,  0
  };

  const uint8_t currentState =
      (digitalRead(clkPin_) << 1) |
       digitalRead(dtPin_);

  const uint8_t transition =
      (lastQuadratureState_ << 2) | currentState;

  const int8_t movement = transitionTable[transition];
  lastQuadratureState_ = currentState;

  if (movement != 0) {
    movementAccumulator_ += movement;

    if (movementAccumulator_ >= AppConfig::ENCODER_STEPS_PER_DETENT) {
      movementAccumulator_ = 0;
      return InputEvent::RotateRight;
    }

    if (movementAccumulator_ <= -static_cast<int8_t>(AppConfig::ENCODER_STEPS_PER_DETENT)) {
      movementAccumulator_ = 0;
      return InputEvent::RotateLeft;
    }
  }

  const bool reading = digitalRead(swPin_);

  if (reading != lastButtonReading_) {
    lastButtonChangeMs_ = millis();
    lastButtonReading_ = reading;
  }

  if (millis() - lastButtonChangeMs_ >= AppConfig::BUTTON_DEBOUNCE_MS &&
      reading != stableButtonState_) {
    stableButtonState_ = reading;

    if (stableButtonState_ == LOW) {
      return InputEvent::Press;
    }
  }

  return InputEvent::None;
}
