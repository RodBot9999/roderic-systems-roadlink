#include "TftRenderer.h"

#include <SPI.h>
#include "SplashImage.h"

namespace {
constexpr int16_t SCREEN_WIDTH = 320;
constexpr int16_t SCREEN_HEIGHT = 240;
constexpr int16_t HEADER_HEIGHT = 47;
constexpr int16_t FOOTER_HEIGHT = 23;
constexpr int16_t CONTENT_Y = HEADER_HEIGHT;
constexpr int16_t CONTENT_HEIGHT = SCREEN_HEIGHT - HEADER_HEIGHT - FOOTER_HEIGHT;
constexpr int16_t FOOTER_Y = SCREEN_HEIGHT - FOOTER_HEIGHT;

constexpr uint16_t COLOR_BG = 0x0000;
constexpr uint16_t COLOR_PANEL = 0x0861;
constexpr uint16_t COLOR_PANEL_ALT = 0x10A2;
constexpr uint16_t COLOR_CYAN = 0xB6DC;
constexpr uint16_t COLOR_CYAN_DARK = 0x2BAC;
constexpr uint16_t COLOR_WHITE = 0xFFFF;
constexpr uint16_t COLOR_MUTED = 0x8410;
constexpr uint16_t COLOR_GREEN = 0x07E0;
constexpr uint16_t COLOR_AMBER = 0xFD20;
constexpr uint16_t COLOR_RED = 0xF800;
constexpr uint16_t COLOR_DARK_RED = 0x5000;

constexpr uint8_t BOOT_MAX_ROWS = 6;
}

TftRenderer::TftRenderer(
    UiModel& uiModel,
    uint8_t chipSelectPin,
    uint8_t dataCommandPin,
    uint8_t resetPin)
  : uiModel_(uiModel),
    tft_(chipSelectPin, dataCommandPin, resetPin) {}

bool TftRenderer::begin(
    uint8_t sckPin,
    uint8_t misoPin,
    uint8_t mosiPin,
    uint8_t rotation,
    uint32_t spiFrequencyHz) {
  SPI.begin(sckPin, misoPin, mosiPin);
  tft_.begin(spiFrequencyHz);
  tft_.setRotation(rotation);
  tft_.setTextWrap(false);
  tft_.fillScreen(COLOR_BG);
  initialized_ = true;
  renderedRevision_ = UINT32_MAX;
  hasCachedFrame_ = false;
  return true;
}

void TftRenderer::showSplash() {
  if (!initialized_) return;
  drawPackedSplash();

  tft_.fillRect(0, SCREEN_HEIGHT - 22, SCREEN_WIDTH, 22, COLOR_BG);
  tft_.drawFastHLine(0, SCREEN_HEIGHT - 22, SCREEN_WIDTH, COLOR_CYAN_DARK);
  tft_.setTextSize(1);
  tft_.setTextColor(COLOR_CYAN, COLOR_BG);
  tft_.setCursor(101, SCREEN_HEIGHT - 14);
  tft_.print(F("PRESS TO CONTINUE"));
}

void TftRenderer::beginSystemCheck() {
  if (!initialized_) return;

  tft_.fillScreen(COLOR_BG);
  tft_.drawFastHLine(0, 0, SCREEN_WIDTH, COLOR_CYAN);
  tft_.drawFastHLine(0, 38, SCREEN_WIDTH, COLOR_CYAN_DARK);

  tft_.setTextColor(COLOR_CYAN, COLOR_BG);
  tft_.setTextSize(2);
  tft_.setCursor(8, 7);
  tft_.print(F("RODERIC SYSTEMS"));

  tft_.setTextSize(1);
  tft_.setTextColor(COLOR_MUTED, COLOR_BG);
  tft_.setCursor(8, 27);
  tft_.print(F("SYSTEM DIAGNOSTICS / BOOT SEQUENCE"));

  for (uint8_t row = 0; row < BOOT_MAX_ROWS; ++row) {
    setSystemCheck(row, "WAITING", "...", BootCheckState::Pending);
  }

  tft_.drawRect(7, 207, 306, 25, COLOR_CYAN_DARK);
  tft_.setCursor(14, 216);
  tft_.setTextColor(COLOR_MUTED, COLOR_BG);
  tft_.print(F("INITIALIZING EMBEDDED SERVICES"));
}

void TftRenderer::setSystemCheck(
    uint8_t row,
    const String& module,
    const String& result,
    BootCheckState state) {
  if (!initialized_ || row >= BOOT_MAX_ROWS) return;

  const int16_t y = 51 + static_cast<int16_t>(row) * 24;
  tft_.fillRect(8, y, 304, 20, COLOR_BG);
  tft_.drawFastHLine(8, y + 19, 304, COLOR_PANEL_ALT);

  tft_.setTextSize(1);
  tft_.setTextColor(COLOR_CYAN, COLOR_BG);
  tft_.setCursor(13, y + 6);
  tft_.print(clippedText(module, 19));

  tft_.setTextColor(COLOR_MUTED, COLOR_BG);
  tft_.setCursor(138, y + 6);
  tft_.print(F("........"));

  const uint16_t color = bootStateColor(state);
  tft_.fillRect(221, y + 2, 86, 16, state == BootCheckState::Error ? COLOR_DARK_RED : COLOR_PANEL);
  tft_.drawRect(221, y + 2, 86, 16, color);
  tft_.setTextColor(color, state == BootCheckState::Error ? COLOR_DARK_RED : COLOR_PANEL);
  tft_.setCursor(227, y + 6);
  tft_.print(clippedText(result, 12));
}

void TftRenderer::finishSystemCheck(const String& message) {
  if (!initialized_) return;

  tft_.fillRect(8, 208, 304, 23, COLOR_BG);
  tft_.drawRect(7, 207, 306, 25, COLOR_CYAN_DARK);
  tft_.setTextColor(statusColor(message), COLOR_BG);
  tft_.setTextSize(1);
  tft_.setCursor(14, 216);
  tft_.print(clippedText(message, 36));
  tft_.setTextColor(COLOR_CYAN, COLOR_BG);
  tft_.setCursor(240, 216);
  tft_.print(F("PRESS: SKIP"));
}

void TftRenderer::update(bool force) {
  if (!initialized_) return;

  const UiFrame& frame = uiModel_.frame();
  const uint32_t revision = uiModel_.revision();
  const uint8_t page = detailPage(frame);
  const bool pageChanged = page != renderedDetailPage_;

  if (!force && revision == renderedRevision_ && !pageChanged) return;

  const bool identityChanged = !hasCachedFrame_ || !sameScreenIdentity(cachedFrame_, frame);
  if (identityChanged) {
    screenStartedMs_ = millis();
  }

  renderFrame(frame, force || identityChanged || pageChanged);
  cachedFrame_ = frame;
  hasCachedFrame_ = true;
  renderedRevision_ = revision;
  renderedDetailPage_ = detailPage(frame);
}

bool TftRenderer::initialized() const {
  return initialized_;
}

void TftRenderer::drawPackedSplash() {
  uint16_t rowBuffer[RODERIC_SPLASH_WIDTH];

  for (uint16_t y = 0; y < RODERIC_SPLASH_HEIGHT; ++y) {
    for (uint16_t x = 0; x < RODERIC_SPLASH_WIDTH; ++x) {
      const uint32_t pixelIndex = static_cast<uint32_t>(y) * RODERIC_SPLASH_WIDTH + x;
      const uint8_t packed = pgm_read_byte(&RODERIC_SPLASH_PIXELS[pixelIndex >> 2]);
      const uint8_t shift = 6 - static_cast<uint8_t>((pixelIndex & 0x03) * 2);
      const uint8_t paletteIndex = (packed >> shift) & 0x03;
      rowBuffer[x] = pgm_read_word(&RODERIC_SPLASH_PALETTE[paletteIndex]);
    }
    tft_.drawRGBBitmap(0, y, rowBuffer, RODERIC_SPLASH_WIDTH, 1);
  }
}

void TftRenderer::renderFrame(const UiFrame& frame, bool fullRedraw) {
  if (fullRedraw) {
    drawShell(frame);

    if (frame.layout == UiLayout::Menu) {
      drawMenu(frame);
    } else if (frame.layout == UiLayout::Alert) {
      drawAlert(frame);
    } else {
      drawDetail(frame);
    }
  } else {
    if (frame.title != cachedFrame_.title ||
        frame.breadcrumb != cachedFrame_.breadcrumb ||
        frame.subtitle != cachedFrame_.subtitle ||
        frame.layout != cachedFrame_.layout) {
      drawHeader(frame);
    }

    if (frame.layout == UiLayout::Menu) {
      updateMenuPartial(frame);
    } else if (frame.layout == UiLayout::Alert) {
      tft_.fillRect(0, CONTENT_Y, SCREEN_WIDTH, CONTENT_HEIGHT, COLOR_BG);
      tft_.drawRect(3, CONTENT_Y, SCREEN_WIDTH - 6, CONTENT_HEIGHT, COLOR_CYAN_DARK);
      drawAlert(frame);
    } else {
      updateDetailPartial(frame);
    }
  }

  // The footer is small, so redrawing it is inexpensive and guarantees
  // the currently selected action is always synchronized with the knob.
  drawFooter(frame);
}

void TftRenderer::drawShell(const UiFrame& frame) {
  tft_.fillScreen(COLOR_BG);
  drawHeader(frame);
  tft_.drawRect(3, CONTENT_Y, SCREEN_WIDTH - 6, CONTENT_HEIGHT, COLOR_CYAN_DARK);
}

void TftRenderer::drawHeader(const UiFrame& frame) {
  tft_.fillRect(0, 0, SCREEN_WIDTH, HEADER_HEIGHT, COLOR_BG);
  tft_.drawFastHLine(0, 0, SCREEN_WIDTH, COLOR_CYAN);
  tft_.drawFastHLine(0, HEADER_HEIGHT - 1, SCREEN_WIDTH, COLOR_CYAN_DARK);

  tft_.setTextSize(2);
  tft_.setTextColor(COLOR_WHITE, COLOR_BG);
  tft_.setCursor(7, 5);
  tft_.print(clippedText(frame.title.length() ? frame.title : "RODERIC SYSTEMS", 25));

  tft_.setTextSize(1);
  tft_.setTextColor(COLOR_CYAN, COLOR_BG);
  tft_.setCursor(8, 25);
  tft_.print(clippedText(frame.breadcrumb.length() ? frame.breadcrumb : "SYSTEM / ROOT", 39));

  const String right = frame.layout == UiLayout::Menu
      ? "MENU"
      : (frame.layout == UiLayout::Alert ? "ALERT" : "DATA");
  tft_.setTextColor(frame.layout == UiLayout::Alert ? COLOR_RED : COLOR_MUTED, COLOR_BG);
  tft_.setCursor(282, 25);
  tft_.print(right);

  if (frame.subtitle.length()) {
    tft_.setTextColor(COLOR_MUTED, COLOR_BG);
    tft_.setCursor(8, 36);
    tft_.print(clippedText(frame.subtitle, 49));
  }
}

void TftRenderer::drawFooter(const UiFrame& frame) {
  tft_.fillRect(0, FOOTER_Y, SCREEN_WIDTH, FOOTER_HEIGHT, COLOR_BG);
  tft_.drawFastHLine(0, FOOTER_Y, SCREEN_WIDTH, COLOR_CYAN_DARK);
  tft_.setTextSize(1);

  if (frame.layout != UiLayout::Menu && frame.itemCount > 0) {
    const uint8_t selected = frame.selectedIndex < frame.itemCount ? frame.selectedIndex : 0;
    const UiItem& item = frame.items[selected];
    const uint16_t color = item.destructive ? COLOR_RED : COLOR_CYAN;
    tft_.setTextColor(color, COLOR_BG);
    tft_.setCursor(7, FOOTER_Y + 8);
    tft_.print(frame.itemCount > 1 ? F("< ") : F("> "));
    tft_.print(clippedText(item.label, 36));
    tft_.setTextColor(COLOR_MUTED, COLOR_BG);
    tft_.setCursor(271, FOOTER_Y + 8);
    tft_.print(F("PRESS"));
  } else {
    tft_.setTextColor(COLOR_MUTED, COLOR_BG);
    tft_.setCursor(7, FOOTER_Y + 8);
    tft_.print(F("ROTATE: NAVIGATE"));
    tft_.setTextColor(COLOR_CYAN, COLOR_BG);
    tft_.setCursor(211, FOOTER_Y + 8);
    tft_.print(F("PRESS: SELECT"));
  }
}

void TftRenderer::drawMenu(const UiFrame& frame) {
  const uint8_t count = frame.itemCount;
  if (count == 0) {
    tft_.setTextColor(COLOR_MUTED, COLOR_BG);
    tft_.setTextSize(2);
    tft_.setCursor(78, 118);
    tft_.print(F("NO OPTIONS"));
    return;
  }

  const uint8_t start = menuStart(frame);

  const int16_t rowHeight = 27;
  for (uint8_t visible = 0; visible < MENU_VISIBLE_ROWS; ++visible) {
    const uint8_t index = start + visible;
    if (index >= count) break;
    drawMenuRow(
        CONTENT_Y + 5 + visible * rowHeight,
        frame.items[index],
        index == frame.selectedIndex,
        index);
  }

  if (count > MENU_VISIBLE_ROWS) {
    drawScrollbar(count, frame.selectedIndex);
  }
}

void TftRenderer::drawDetail(const UiFrame& frame) {
  const uint8_t page = detailPage(frame);
  const uint8_t pageCount = frame.fieldCount == 0
      ? 1
      : static_cast<uint8_t>((frame.fieldCount + DETAIL_FIELDS_PER_PAGE - 1) / DETAIL_FIELDS_PER_PAGE);
  const uint8_t firstField = page * DETAIL_FIELDS_PER_PAGE;
  const int16_t cellWidth = 154;
  const int16_t cellHeight = 31;

  for (uint8_t local = 0; local < DETAIL_FIELDS_PER_PAGE; ++local) {
    const uint8_t index = firstField + local;
    if (index >= frame.fieldCount) break;
    const int16_t column = local & 1;
    const int16_t row = local >> 1;
    drawFieldCell(
        6 + column * 156,
        CONTENT_Y + 5 + row * cellHeight,
        cellWidth,
        frame.fields[index]);
  }

  if (frame.fieldCount == 0) {
    tft_.setTextColor(COLOR_MUTED, COLOR_BG);
    tft_.setTextSize(2);
    tft_.setCursor(84, 112);
    tft_.print(F("NO DATA"));
  }

  if (pageCount > 1) {
    tft_.setTextSize(1);
    tft_.setTextColor(COLOR_AMBER, COLOR_BG);
    tft_.fillRect(282, 35, 34, 10, COLOR_BG);
    tft_.setCursor(286, 36);
    tft_.print(String(page + 1) + "/" + String(pageCount));
  }

  drawDetailStatus(frame);
}

void TftRenderer::updateMenuPartial(const UiFrame& frame) {
  const uint8_t oldStart = menuStart(cachedFrame_);
  const uint8_t newStart = menuStart(frame);

  if (oldStart != newStart || cachedFrame_.itemCount != frame.itemCount) {
    tft_.fillRect(4, CONTENT_Y + 1, SCREEN_WIDTH - 8, CONTENT_HEIGHT - 2, COLOR_BG);
    drawMenu(frame);
    return;
  }

  const int16_t rowHeight = 27;
  for (uint8_t visible = 0; visible < MENU_VISIBLE_ROWS; ++visible) {
    const uint8_t index = newStart + visible;
    if (index >= frame.itemCount) break;

    const bool wasSelected = index == cachedFrame_.selectedIndex;
    const bool isSelected = index == frame.selectedIndex;
    if (wasSelected != isSelected || !sameItem(cachedFrame_.items[index], frame.items[index])) {
      drawMenuRow(
          CONTENT_Y + 5 + visible * rowHeight,
          frame.items[index],
          isSelected,
          index);
    }
  }

  if (frame.itemCount > MENU_VISIBLE_ROWS &&
      frame.selectedIndex != cachedFrame_.selectedIndex) {
    tft_.fillRect(307, CONTENT_Y + 3, 9, CONTENT_HEIGHT - 6, COLOR_BG);
    drawScrollbar(frame.itemCount, frame.selectedIndex);
  }
}

void TftRenderer::updateDetailPartial(const UiFrame& frame) {
  const uint8_t page = detailPage(frame);
  const uint8_t firstField = page * DETAIL_FIELDS_PER_PAGE;
  const int16_t cellWidth = 154;
  const int16_t cellHeight = 31;

  for (uint8_t local = 0; local < DETAIL_FIELDS_PER_PAGE; ++local) {
    const uint8_t index = firstField + local;
    if (index >= frame.fieldCount) break;

    if (!sameField(cachedFrame_.fields[index], frame.fields[index])) {
      const int16_t column = local & 1;
      const int16_t row = local >> 1;
      drawFieldCell(
          6 + column * 156,
          CONTENT_Y + 5 + row * cellHeight,
          cellWidth,
          frame.fields[index]);
    }
  }

  if (frame.status != cachedFrame_.status) {
    drawDetailStatus(frame);
  }
}

void TftRenderer::drawDetailStatus(const UiFrame& frame) {
  tft_.fillRect(7, CONTENT_Y + CONTENT_HEIGHT - 14, 306, 13, COLOR_BG);
  if (!frame.status.length()) return;

  tft_.setTextSize(1);
  tft_.setTextColor(statusColor(frame.status), COLOR_BG);
  tft_.setCursor(8, CONTENT_Y + CONTENT_HEIGHT - 11);
  tft_.print(clippedText(frame.status, 49));
}

void TftRenderer::drawAlert(const UiFrame& frame) {
  tft_.drawRect(6, CONTENT_Y + 5, 308, CONTENT_HEIGHT - 10, COLOR_RED);
  tft_.drawRect(7, CONTENT_Y + 6, 306, CONTENT_HEIGHT - 12, COLOR_DARK_RED);

  tft_.setTextSize(2);
  tft_.setTextColor(COLOR_RED, COLOR_BG);
  tft_.setCursor(16, CONTENT_Y + 14);
  tft_.print(clippedText(frame.subtitle.length() ? frame.subtitle : "SYSTEM ALERT", 23));

  tft_.setTextSize(1);
  for (uint8_t index = 0; index < frame.fieldCount && index < 7; ++index) {
    const int16_t y = CONTENT_Y + 45 + index * 17;
    tft_.setTextColor(COLOR_MUTED, COLOR_BG);
    tft_.setCursor(17, y);
    tft_.print(clippedText(frame.fields[index].label, 20));
    tft_.setTextColor(COLOR_WHITE, COLOR_BG);
    tft_.setCursor(158, y);
    tft_.print(clippedText(frame.fields[index].value, 23));
  }

  if (frame.status.length()) {
    tft_.setTextColor(COLOR_AMBER, COLOR_BG);
    tft_.setCursor(17, CONTENT_Y + CONTENT_HEIGHT - 20);
    tft_.print(clippedText(frame.status, 45));
  }
}

void TftRenderer::drawFieldCell(
    int16_t x,
    int16_t y,
    int16_t width,
    const UiField& field) {
  tft_.fillRect(x, y, width, 28, COLOR_PANEL);
  tft_.drawRect(x, y, width, 28, COLOR_PANEL_ALT);

  tft_.setTextSize(1);
  tft_.setTextColor(COLOR_CYAN, COLOR_PANEL);
  tft_.setCursor(x + 5, y + 4);
  tft_.print(clippedText(field.label, 23));

  tft_.setTextColor(COLOR_WHITE, COLOR_PANEL);
  tft_.setCursor(x + 5, y + 16);
  tft_.print(clippedText(field.value, 23));
}

void TftRenderer::drawMenuRow(
    int16_t y,
    const UiItem& item,
    bool selected,
    uint8_t absoluteIndex) {
  const uint16_t background = selected ? COLOR_CYAN_DARK : COLOR_PANEL;
  const uint16_t border = item.destructive ? COLOR_RED : (selected ? COLOR_CYAN : COLOR_PANEL_ALT);
  const uint16_t text = !item.enabled
      ? COLOR_MUTED
      : (item.destructive ? COLOR_RED : COLOR_WHITE);

  tft_.fillRect(7, y, 298, 23, background);
  tft_.drawRect(7, y, 298, 23, border);

  if (selected) {
    tft_.fillRect(7, y, 4, 23, item.destructive ? COLOR_RED : COLOR_CYAN);
  }

  tft_.setTextSize(1);
  tft_.setTextColor(COLOR_MUTED, background);
  tft_.setCursor(15, y + 8);
  if (absoluteIndex + 1 < 10) tft_.print('0');
  tft_.print(absoluteIndex + 1);

  tft_.setTextColor(text, background);
  tft_.setCursor(37, y + 8);
  tft_.print(clippedText(item.label, item.value.length() ? 28 : 40));

  if (item.value.length()) {
    tft_.setTextColor(selected ? COLOR_CYAN : COLOR_MUTED, background);
    const String value = clippedText(item.value, 15);
    const int16_t x = 300 - static_cast<int16_t>(value.length()) * 6;
    tft_.setCursor(x, y + 8);
    tft_.print(value);
  }
}

void TftRenderer::drawScrollbar(uint8_t itemCount, uint8_t selectedIndex) {
  const int16_t trackY = CONTENT_Y + 6;
  const int16_t trackH = CONTENT_HEIGHT - 12;
  tft_.drawFastVLine(311, trackY, trackH, COLOR_PANEL_ALT);

  const int16_t calculatedThumb = (trackH * MENU_VISIBLE_ROWS) / itemCount;
  const int16_t thumbH = calculatedThumb < 12 ? 12 : calculatedThumb;
  const int16_t travel = trackH - thumbH;
  const int16_t thumbY = trackY + (itemCount > 1
      ? (travel * selectedIndex) / (itemCount - 1)
      : 0);
  tft_.fillRect(309, thumbY, 5, thumbH, COLOR_CYAN);
}

String TftRenderer::cleanText(const String& value) const {
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

String TftRenderer::clippedText(const String& value, uint8_t maxChars) const {
  String output = cleanText(value);
  if (output.length() <= maxChars) return output;
  if (maxChars <= 3) return output.substring(0, maxChars);
  return output.substring(0, maxChars - 3) + "...";
}

uint16_t TftRenderer::statusColor(const String& value) const {
  String lowered = value;
  lowered.toLowerCase();
  if (lowered.indexOf("error") >= 0 ||
      lowered.indexOf("fail") >= 0 ||
      lowered.indexOf("offline") >= 0 ||
      lowered.indexOf("no fix") >= 0) {
    return COLOR_RED;
  }
  if (lowered.indexOf("wait") >= 0 ||
      lowered.indexOf("pending") >= 0 ||
      lowered.indexOf("listen") >= 0) {
    return COLOR_AMBER;
  }
  if (lowered.indexOf("ok") >= 0 ||
      lowered.indexOf("ready") >= 0 ||
      lowered.indexOf("active") >= 0 ||
      lowered.indexOf("valid") >= 0) {
    return COLOR_GREEN;
  }
  return COLOR_MUTED;
}

uint16_t TftRenderer::bootStateColor(BootCheckState state) const {
  switch (state) {
    case BootCheckState::Ok:      return COLOR_GREEN;
    case BootCheckState::Warning: return COLOR_AMBER;
    case BootCheckState::Error:   return COLOR_RED;
    case BootCheckState::Off:     return COLOR_MUTED;
    case BootCheckState::Pending: return COLOR_CYAN;
  }
  return COLOR_MUTED;
}

bool TftRenderer::sameScreenIdentity(const UiFrame& left, const UiFrame& right) const {
  return left.layout == right.layout &&
      left.title == right.title &&
      left.breadcrumb == right.breadcrumb &&
      left.itemCount == right.itemCount &&
      left.fieldCount == right.fieldCount;
}

uint8_t TftRenderer::detailPage(const UiFrame& frame) const {
  if (frame.layout == UiLayout::Menu || frame.fieldCount <= DETAIL_FIELDS_PER_PAGE) {
    return 0;
  }

  const uint8_t pageCount = static_cast<uint8_t>(
      (frame.fieldCount + DETAIL_FIELDS_PER_PAGE - 1) / DETAIL_FIELDS_PER_PAGE);
  if (pageCount <= 1) return 0;
  return static_cast<uint8_t>(((millis() - screenStartedMs_) / DETAIL_PAGE_INTERVAL_MS) % pageCount);
}


uint8_t TftRenderer::menuStart(const UiFrame& frame) const {
  if (frame.itemCount <= MENU_VISIBLE_ROWS) return 0;

  const uint8_t half = MENU_VISIBLE_ROWS / 2;
  uint8_t start = frame.selectedIndex > half
      ? frame.selectedIndex - half
      : 0;
  if (start + MENU_VISIBLE_ROWS > frame.itemCount) {
    start = frame.itemCount - MENU_VISIBLE_ROWS;
  }
  return start;
}

bool TftRenderer::sameItem(const UiItem& left, const UiItem& right) const {
  return left.label == right.label &&
      left.value == right.value &&
      left.enabled == right.enabled &&
      left.destructive == right.destructive;
}

bool TftRenderer::sameField(const UiField& left, const UiField& right) const {
  return left.label == right.label && left.value == right.value;
}
