#include "UiModel.h"

void UiModel::begin(bool serialMirrorEnabled) {
  serialMirrorEnabled_ = serialMirrorEnabled;
}

void UiModel::publish(const UiFrame& frame, bool force) {
  if (!force && hasFrame_ && sameContent(frame_, frame)) {
    return;
  }

  frame_ = frame;
  frame_.generatedMs = millis();
  revision_++;
  hasFrame_ = true;

  if (serialMirrorEnabled_) {
    printFrameToSerial(frame_);
  }
}

void UiModel::setSerialMirror(bool enabled) {
  serialMirrorEnabled_ = enabled;

  if (enabled && hasFrame_) {
    printFrameToSerial(frame_);
  }
}

bool UiModel::serialMirrorEnabled() const {
  return serialMirrorEnabled_;
}

const UiFrame& UiModel::frame() const {
  return frame_;
}

uint32_t UiModel::revision() const {
  return revision_;
}

bool UiModel::sameContent(const UiFrame& left, const UiFrame& right) const {
  if (left.layout != right.layout ||
      left.title != right.title ||
      left.breadcrumb != right.breadcrumb ||
      left.subtitle != right.subtitle ||
      left.status != right.status ||
      left.itemCount != right.itemCount ||
      left.selectedIndex != right.selectedIndex ||
      left.fieldCount != right.fieldCount ||
      left.canGoBack != right.canGoBack) {
    return false;
  }

  for (uint8_t index = 0; index < left.itemCount; ++index) {
    if (left.items[index].label != right.items[index].label ||
        left.items[index].value != right.items[index].value ||
        left.items[index].enabled != right.items[index].enabled ||
        left.items[index].destructive != right.items[index].destructive) {
      return false;
    }
  }

  for (uint8_t index = 0; index < left.fieldCount; ++index) {
    if (left.fields[index].label != right.fields[index].label ||
        left.fields[index].value != right.fields[index].value) {
      return false;
    }
  }

  return true;
}

void UiModel::printFrameToSerial(const UiFrame& frame) const {
  Serial.println();
  Serial.println(F("============================================================"));
  Serial.print(F("[UI "));
  Serial.print(layoutLabel(frame.layout));
  Serial.print(F("] "));
  Serial.println(frame.title);

  if (frame.breadcrumb.length()) {
    Serial.print(F("Path: "));
    Serial.println(frame.breadcrumb);
  }

  if (frame.subtitle.length()) {
    Serial.println(frame.subtitle);
  }

  if (frame.fieldCount > 0) {
    Serial.println(F("-- Data -----------------------------------------------------"));
    for (uint8_t index = 0; index < frame.fieldCount; ++index) {
      Serial.print(frame.fields[index].label);
      Serial.print(F(": "));
      Serial.println(frame.fields[index].value);
    }
  }

  if (frame.itemCount > 0) {
    Serial.println(F("-- Options --------------------------------------------------"));
    for (uint8_t index = 0; index < frame.itemCount; ++index) {
      Serial.print(index == frame.selectedIndex ? F("> ") : F("  "));
      Serial.print(frame.items[index].label);
      if (frame.items[index].value.length()) {
        Serial.print(F("  ["));
        Serial.print(frame.items[index].value);
        Serial.print(']');
      }
      if (!frame.items[index].enabled) {
        Serial.print(F("  (disabled)"));
      }
      Serial.println();
    }
  }

  if (frame.status.length()) {
    Serial.print(F("Status: "));
    Serial.println(frame.status);
  }
  Serial.println(F("============================================================"));
}

const char* UiModel::layoutLabel(UiLayout layout) const {
  switch (layout) {
    case UiLayout::Menu:   return "MENU";
    case UiLayout::Detail: return "DETAIL";
    case UiLayout::Alert:  return "ALERT";
  }
  return "UNKNOWN";
}
