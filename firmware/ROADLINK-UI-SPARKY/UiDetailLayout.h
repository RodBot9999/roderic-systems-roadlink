#pragma once

#include "UiModel.h"

// Static, knob-controlled pages. Long values continue on another card/page;
// there is no marquee, timer, or automatic page advance.
constexpr uint8_t UI_WIDE_CARDS_PER_PAGE = 3;
constexpr uint8_t UI_WIDE_LINE_CHARS = 48;
constexpr uint16_t UI_WIDE_CHARS_PER_CARD = 3 * UI_WIDE_LINE_CHARS;

inline String uiDisplayText(const String& value) {
  String output = value;
  output.replace("°C", " C");
  output.replace("°", " deg");
  output.replace("•", "/");
  output.replace("–", "-");
  output.replace("—", "-");
  String ascii;
  ascii.reserve(output.length());
  for (uint16_t index = 0; index < output.length(); ++index) {
    const uint8_t character = static_cast<uint8_t>(output[index]);
    if (character >= 32 && character <= 126) ascii += static_cast<char>(character);
  }
  return ascii;
}

inline bool uiUsesWideDetail(const UiFrame& frame) {
  return frame.layout == UiLayout::Detail && frame.itemCount <= 1;
}

inline uint8_t uiWideCardCount(const String& value) {
  const size_t length = uiDisplayText(value).length();
  return length == 0 ? 1 : static_cast<uint8_t>(
      (length + UI_WIDE_CHARS_PER_CARD - 1) / UI_WIDE_CHARS_PER_CARD);
}

inline uint8_t uiWidePageCount(const UiFrame& frame) {
  uint16_t cards = 0;
  for (uint8_t index = 0; index < frame.fieldCount; ++index) {
    cards += uiWideCardCount(frame.fields[index].value);
  }
  return cards == 0 ? 1 : static_cast<uint8_t>(
      (cards + UI_WIDE_CARDS_PER_PAGE - 1) / UI_WIDE_CARDS_PER_PAGE);
}

inline uint8_t uiDetailPageCount(const UiFrame& frame) {
  if (uiUsesWideDetail(frame)) return uiWidePageCount(frame);
  return frame.fieldCount == 0 ? 1 : static_cast<uint8_t>(
      (frame.fieldCount + UI_DETAIL_FIELDS_PER_PAGE - 1) /
      UI_DETAIL_FIELDS_PER_PAGE);
}

inline bool uiWideCardAt(
    const UiFrame& frame, uint16_t cardIndex, String& label, String& value) {
  for (uint8_t field = 0; field < frame.fieldCount; ++field) {
    const String display = uiDisplayText(frame.fields[field].value);
    const uint8_t count = display.length() == 0 ? 1 : static_cast<uint8_t>(
        (display.length() + UI_WIDE_CHARS_PER_CARD - 1) /
        UI_WIDE_CHARS_PER_CARD);
    if (cardIndex < count) {
      label = uiDisplayText(frame.fields[field].label);
      if (cardIndex > 0) label += " (continued)";
      const uint16_t first = cardIndex * UI_WIDE_CHARS_PER_CARD;
      value = display.substring(first, first + UI_WIDE_CHARS_PER_CARD);
      return true;
    }
    cardIndex -= count;
  }
  return false;
}
