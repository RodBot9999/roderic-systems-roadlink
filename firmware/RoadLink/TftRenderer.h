#pragma once

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include "UiModel.h"

enum class BootCheckState : uint8_t {
  Pending,
  Ok,
  Warning,
  Error,
  Off
};

class TftRenderer {
public:
  TftRenderer(
      UiModel& uiModel,
      uint8_t chipSelectPin,
      uint8_t dataCommandPin,
      uint8_t resetPin);

  bool begin(
      uint8_t sckPin,
      uint8_t misoPin,
      uint8_t mosiPin,
      uint8_t rotation,
      uint32_t spiFrequencyHz);

  void showSplash();
  void beginSystemCheck();
  void setSystemCheck(
      uint8_t row,
      const String& module,
      const String& result,
      BootCheckState state);
  void finishSystemCheck(const String& message);

  void update(bool force = false);
  bool initialized() const;

private:
  static constexpr uint8_t MENU_VISIBLE_ROWS = 6;
  static constexpr uint8_t DETAIL_FIELDS_PER_PAGE = 10;
  static constexpr uint32_t DETAIL_PAGE_INTERVAL_MS = 2600;

  void drawPackedSplash();
  void renderFrame(const UiFrame& frame, bool fullRedraw);
  void drawShell(const UiFrame& frame);
  void drawHeader(const UiFrame& frame);
  void drawFooter(const UiFrame& frame);
  void drawMenu(const UiFrame& frame);
  void drawDetail(const UiFrame& frame);
  void drawAlert(const UiFrame& frame);
  void updateMenuPartial(const UiFrame& frame);
  void updateDetailPartial(const UiFrame& frame);
  void drawDetailStatus(const UiFrame& frame);
  void drawFieldCell(
      int16_t x,
      int16_t y,
      int16_t width,
      const UiField& field);
  void drawMenuRow(
      int16_t y,
      const UiItem& item,
      bool selected,
      uint8_t absoluteIndex);
  void drawScrollbar(uint8_t itemCount, uint8_t selectedIndex);

  String cleanText(const String& value) const;
  String clippedText(const String& value, uint8_t maxChars) const;
  uint16_t statusColor(const String& value) const;
  uint16_t bootStateColor(BootCheckState state) const;
  bool sameScreenIdentity(const UiFrame& left, const UiFrame& right) const;
  uint8_t detailPage(const UiFrame& frame) const;
  uint8_t menuStart(const UiFrame& frame) const;
  bool sameItem(const UiItem& left, const UiItem& right) const;
  bool sameField(const UiField& left, const UiField& right) const;

  UiModel& uiModel_;
  Adafruit_ILI9341 tft_;
  bool initialized_ = false;
  bool hasCachedFrame_ = false;
  UiFrame cachedFrame_;
  uint32_t renderedRevision_ = UINT32_MAX;
  uint32_t screenStartedMs_ = 0;
  uint8_t renderedDetailPage_ = 0xFF;
};
