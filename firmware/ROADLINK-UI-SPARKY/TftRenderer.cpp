#include "TftRenderer.h"

#include <SPI.h>
#include "SplashImage.h"
#include "UiDetailLayout.h"

namespace {
constexpr int16_t SCREEN_WIDTH = 320;
constexpr int16_t SCREEN_HEIGHT = 240;
constexpr int16_t HEADER_HEIGHT = 54;
constexpr int16_t FOOTER_HEIGHT = 23;
constexpr int16_t CONTENT_Y = HEADER_HEIGHT;
constexpr int16_t CONTENT_HEIGHT = SCREEN_HEIGHT - HEADER_HEIGHT - FOOTER_HEIGHT;
constexpr int16_t FOOTER_Y = SCREEN_HEIGHT - FOOTER_HEIGHT;

// A code-drawn theme keeps the visual redesign small enough for both OTA slots.
uint16_t COLOR_BG = 0x0063;          // midnight ink
uint16_t COLOR_PANEL = 0x08E5;       // slate card
uint16_t COLOR_PANEL_ALT = 0x1168;   // raised card
uint16_t COLOR_LINE = 0x2A6B;
uint16_t COLOR_CYAN = 0x5F39;        // theme accent
uint16_t COLOR_CYAN_DARK = 0x1A89;
uint16_t COLOR_SELECT = 0xFDEB;
constexpr uint16_t COLOR_WHITE = 0xEFBE;
constexpr uint16_t COLOR_MUTED = 0x8D56;
constexpr uint16_t COLOR_GREEN = 0x7733;
constexpr uint16_t COLOR_AMBER = 0xFDEB;
constexpr uint16_t COLOR_RED = 0xFB4E;
constexpr uint16_t COLOR_DARK_RED = 0x48E5;
constexpr uint16_t COLOR_DARK_GREEN = 0x12A8;
constexpr uint16_t COLOR_DARK_AMBER = 0x59C3;

// The original uploaded splash and its caption must remain pixel-for-pixel.
constexpr uint16_t SPLASH_BG = 0x0000;
constexpr uint16_t SPLASH_LINE = 0x2BAC;
constexpr uint16_t SPLASH_TEXT = 0xB6DC;

constexpr uint8_t BOOT_MAX_ROWS = 6;
struct ThemeColors {
  uint16_t bg, panel, raised, line, accent, dim, selection;
};
constexpr ThemeColors THEMES[] = {
    {0x0063, 0x08E5, 0x1168, 0x2A6B, 0x5F39, 0x1A89, 0xFDEB},
    {0x0084, 0x0907, 0x11CA, 0x2A8E, 0x5E3F, 0x122C, 0xFE2C},
    {0x1061, 0x30C2, 0x4103, 0x69E4, 0xFC89, 0x6983, 0x5F39},
    {0x0843, 0x20A6, 0x3109, 0x51CD, 0xD47F, 0x516D, 0xFE2C},
    {0x0862, 0x18E4, 0x2986, 0x4ACB, 0xDF7D, 0x3A89, 0xFE2C}};
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

  tft_.fillRect(0, SCREEN_HEIGHT - 22, SCREEN_WIDTH, 22, SPLASH_BG);
  tft_.drawFastHLine(0, SCREEN_HEIGHT - 22, SCREEN_WIDTH, SPLASH_LINE);
  tft_.setTextSize(1);
  tft_.setTextColor(SPLASH_TEXT, SPLASH_BG);
  tft_.setCursor(101, SCREEN_HEIGHT - 14);
  tft_.print(F("PRESS TO CONTINUE"));
}

void TftRenderer::beginSystemCheck() {
  if (!initialized_) return;

  tft_.fillScreen(COLOR_BG);
  tft_.fillRect(0, 0, SCREEN_WIDTH, 3, COLOR_CYAN);
  tft_.fillRoundRect(8, 10, 28, 28, 5, COLOR_CYAN_DARK);
  tft_.drawLine(24, 13, 16, 26, COLOR_CYAN);
  tft_.drawLine(16, 26, 24, 24, COLOR_CYAN);
  tft_.drawLine(24, 24, 20, 35, COLOR_CYAN);
  tft_.setTextSize(2);
  tft_.setTextColor(COLOR_WHITE, COLOR_BG);
  tft_.setCursor(44, 8);
  tft_.print(F("BOOT CHECK"));
  tft_.setTextSize(1);
  tft_.setTextColor(COLOR_MUTED, COLOR_BG);
  tft_.setCursor(45, 29);
  tft_.print(F("RODERIC SYSTEMS  /  06 MODULES"));
  tft_.drawFastHLine(8, 44, 304, COLOR_LINE);

  for (uint8_t row = 0; row < BOOT_MAX_ROWS; ++row) {
    setSystemCheck(row, "WAITING", "...", BootCheckState::Pending);
  }

  tft_.fillRoundRect(8, 206, 304, 27, 5, COLOR_PANEL);
  tft_.setCursor(17, 215);
  tft_.setTextColor(COLOR_MUTED, COLOR_PANEL);
  tft_.print(F("INITIALIZING EMBEDDED SERVICES"));
}

void TftRenderer::setSystemCheck(
    uint8_t row,
    const String& module,
    const String& result,
    BootCheckState state) {
  if (!initialized_ || row >= BOOT_MAX_ROWS) return;

  const int16_t y = 49 + static_cast<int16_t>(row) * 25;
  tft_.fillRoundRect(8, y, 304, 22, 4, COLOR_PANEL);
  tft_.fillRoundRect(12, y + 3, 18, 16, 3, COLOR_PANEL_ALT);
  tft_.setTextSize(1);
  tft_.setTextColor(COLOR_CYAN, COLOR_PANEL_ALT);
  tft_.setCursor(15, y + 7);
  tft_.print(row + 1);
  tft_.setTextColor(COLOR_WHITE, COLOR_PANEL);
  tft_.setCursor(38, y + 8);
  tft_.print(clippedText(module, 22));
  const uint16_t color = bootStateColor(state);
  const uint16_t badge = state == BootCheckState::Error ? COLOR_DARK_RED
      : (state == BootCheckState::Ok ? COLOR_DARK_GREEN : COLOR_PANEL_ALT);
  tft_.fillRoundRect(224, y + 3, 83, 16, 4, badge);
  tft_.setTextColor(color, badge);
  tft_.setCursor(230, y + 7);
  tft_.print(clippedText(result, 12));
}

void TftRenderer::finishSystemCheck(const String& message) {
  if (!initialized_) return;

  tft_.fillRoundRect(8, 206, 304, 27, 5, COLOR_PANEL);
  tft_.fillRect(8, 210, 3, 18, statusColor(message));
  tft_.setTextColor(statusColor(message), COLOR_PANEL);
  tft_.setTextSize(1);
  tft_.setCursor(17, 216);
  tft_.print(clippedText(message, 35));
  tft_.setTextColor(COLOR_CYAN, COLOR_PANEL);
  tft_.setCursor(241, 216);
  tft_.print(F("PRESS SKIP"));
}

void TftRenderer::update(bool force) {
  if (!initialized_) return;

  const UiFrame& frame = uiModel_.frame();
  if (frame.themeId != themeId_) setTheme(frame.themeId);
  const uint32_t revision = uiModel_.revision();
  const uint8_t page = detailPage(frame);
  const bool pageChanged = page != renderedDetailPage_;

  if (!force && revision == renderedRevision_ && !pageChanged) return;

  const bool identityChanged = !hasCachedFrame_ || !sameScreenIdentity(cachedFrame_, frame);

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
    } else if (frame.layout == UiLayout::Tiles) {
      drawTiles(frame);
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
    } else if (frame.layout == UiLayout::Tiles) {
      tft_.fillRect(4, CONTENT_Y + 1, SCREEN_WIDTH - 8, CONTENT_HEIGHT - 2, COLOR_BG);
      drawTiles(frame);
    } else if (frame.layout == UiLayout::Alert) {
      tft_.fillRect(0, CONTENT_Y, SCREEN_WIDTH, CONTENT_HEIGHT, COLOR_BG);
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
}

void TftRenderer::setOtaAvailable(bool available) {
  if (otaAvailable_ == available) return;
  otaAvailable_ = available;
  hasCachedFrame_ = false;
  renderedRevision_ = UINT32_MAX;
}

void TftRenderer::setTheme(uint8_t theme) {
  if (theme > 4) theme = 0;
  if (themeId_ == theme) return;
  themeId_ = theme;
  const ThemeColors& palette = THEMES[theme];
  COLOR_BG = palette.bg;
  COLOR_PANEL = palette.panel;
  COLOR_PANEL_ALT = palette.raised;
  COLOR_LINE = palette.line;
  COLOR_CYAN = palette.accent;
  COLOR_CYAN_DARK = palette.dim;
  COLOR_SELECT = palette.selection;
  hasCachedFrame_ = false;
  renderedRevision_ = UINT32_MAX;
}

void TftRenderer::drawHeader(const UiFrame& frame) {
  tft_.fillRect(0, 0, SCREEN_WIDTH, HEADER_HEIGHT, COLOR_BG);
  tft_.fillRect(0, 0, SCREEN_WIDTH, 3, frame.layout == UiLayout::Alert
      ? COLOR_RED : COLOR_CYAN);
  tft_.fillRoundRect(7, 8, 26, 24, 5, COLOR_CYAN_DARK);
  tft_.drawLine(22, 10, 15, 22, COLOR_CYAN);
  tft_.drawLine(15, 22, 22, 20, COLOR_CYAN);
  tft_.drawLine(22, 20, 19, 30, COLOR_CYAN);

  const String title = cleanText(frame.title.length() ? frame.title : "ROADLINK");
  tft_.setTextColor(COLOR_WHITE, COLOR_BG);
  tft_.setTextSize(title.length() <= 18 ? 2 : 1);
  tft_.setCursor(40, title.length() <= 18 ? 8 : 13);
  tft_.print(clippedText(title, title.length() <= 18 ? 18 : 37));

  // OTA is a separate local service, not a duplicate phone dashboard.
  const uint16_t otaBackground = otaAvailable_ ? COLOR_DARK_GREEN : COLOR_PANEL;
  const uint16_t otaColor = otaAvailable_ ? COLOR_GREEN : COLOR_MUTED;
  tft_.fillRoundRect(272, 10, 41, 17, 4, otaBackground);
  tft_.setTextSize(1);
  tft_.fillCircle(279, 18, 2, otaColor);
  tft_.setTextColor(otaColor, otaBackground);
  tft_.setCursor(285, 15);
  tft_.print(F("OTA"));

  tft_.setTextColor(COLOR_CYAN, COLOR_BG);
  tft_.setCursor(8, 33);
  tft_.print(clippedText(frame.breadcrumb.length() ? frame.breadcrumb : "ROADLINK", 50));
  if (frame.subtitle.length()) {
    tft_.setTextColor(COLOR_MUTED, COLOR_BG);
    tft_.setCursor(8, 44);
    tft_.print(clippedText(frame.subtitle, 50));
  }
  tft_.drawFastHLine(8, HEADER_HEIGHT - 1, 304, COLOR_LINE);
}

void TftRenderer::drawFooter(const UiFrame& frame) {
  tft_.fillRect(0, FOOTER_Y, SCREEN_WIDTH, FOOTER_HEIGHT, COLOR_PANEL);
  tft_.drawFastHLine(0, FOOTER_Y, SCREEN_WIDTH, COLOR_LINE);
  tft_.setTextSize(1);

  const uint8_t detailPageCount = uiDetailPageCount(frame);

  if (frame.layout == UiLayout::Tiles) {
    tft_.setTextColor(COLOR_MUTED, COLOR_PANEL);
    tft_.setCursor(7, FOOTER_Y + 8);
    tft_.print(frame.canGoBack ? F("HOLD  BACK") : F("ROTATE  SELECT"));
    tft_.setTextColor(COLOR_CYAN, COLOR_PANEL);
    tft_.setCursor(230, FOOTER_Y + 8);
    tft_.print(F("PRESS  OPEN"));
  } else if (frame.layout != UiLayout::Menu &&
      detailPageCount > 1 &&
      frame.itemCount <= 1) {
    tft_.setTextColor(COLOR_MUTED, COLOR_PANEL);
    tft_.setCursor(7, FOOTER_Y + 8);
    tft_.print(F("ROTATE  PAGES"));
    tft_.setTextColor(COLOR_CYAN, COLOR_PANEL);
    tft_.setCursor(231, FOOTER_Y + 8);
    tft_.print(F("PRESS  BACK"));
  } else if (frame.layout != UiLayout::Menu && frame.itemCount > 0) {
    const uint8_t selected = frame.selectedIndex < frame.itemCount ? frame.selectedIndex : 0;
    const UiItem& item = frame.items[selected];
    const uint16_t color = item.destructive ? COLOR_RED
        : (item.positive ? COLOR_GREEN : COLOR_CYAN);
    tft_.setTextColor(color, COLOR_PANEL);
    tft_.setCursor(7, FOOTER_Y + 8);
    tft_.print(F("SELECTED  "));
    tft_.print(clippedText(item.label, 30));
    tft_.setTextColor(COLOR_MUTED, COLOR_PANEL);
    tft_.setCursor(278, FOOTER_Y + 8);
    tft_.print(F("PRESS"));
  } else {
    tft_.setTextColor(COLOR_MUTED, COLOR_PANEL);
    tft_.setCursor(7, FOOTER_Y + 8);
    tft_.print(frame.canGoBack ? F("ROTATE  NAV  /  HOLD  BACK")
                               : F("ROTATE  NAV"));
    tft_.setTextColor(COLOR_CYAN, COLOR_PANEL);
    tft_.setCursor(230, FOOTER_Y + 8);
    tft_.print(F("PRESS  OPEN"));
  }
}

void TftRenderer::drawMenu(const UiFrame& frame) {
  if (isHome(frame)) {
    drawHome(frame);
    return;
  }
  const uint8_t count = frame.itemCount;
  if (count == 0) {
    tft_.setTextColor(COLOR_MUTED, COLOR_BG);
    tft_.setTextSize(2);
    tft_.setCursor(78, 118);
    tft_.print(F("NO OPTIONS"));
    return;
  }

  const uint8_t start = menuStart(frame);

  const int16_t rowHeight = 25;
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

bool TftRenderer::isHome(const UiFrame& frame) const {
  return frame.layout == UiLayout::Menu &&
      frame.title == "Scan-Track-Log" && frame.itemCount == 6;
}

void TftRenderer::drawHome(const UiFrame& frame) {
  for (uint8_t index = 0; index < 6; ++index) {
    drawHomeCard(frame, index);
  }
}

void TftRenderer::drawHomeCard(const UiFrame& frame, uint8_t index) {
  static const char* LABELS[] = {
      "STREAMING", "CAN + OBD", "GPS", "CELLULAR", "SETTINGS", "REBOOT"};
  static const char* SUBLABELS[] = {
      "SEND CONTROL", "VEHICLE BUS", "POSITION / FIX", "A7670SA LTE",
      "DEVICE OPTIONS", "RESTART SYSTEM"};
  const int16_t x = 7 + (index & 1) * 156;
  const int16_t y = CONTENT_Y + 5 + (index >> 1) * 53;
  const bool selected = index == frame.selectedIndex;
  const uint16_t background = selected ? COLOR_PANEL_ALT : COLOR_PANEL;
  const uint16_t accent = index == 5 ? COLOR_RED
      : (selected ? COLOR_SELECT : COLOR_CYAN);
  tft_.fillRect(x, y, 150, 48, COLOR_BG);
  tft_.fillRoundRect(x, y, 150, 48, 6, background);
  tft_.drawRoundRect(x, y, 150, 48, 6, selected ? accent : COLOR_LINE);
  if (selected) tft_.fillRoundRect(x, y + 5, 3, 38, 1, accent);
  tft_.fillRoundRect(x + 8, y + 8, 31, 31, 5, COLOR_CYAN_DARK);
  drawHomeGlyph(index, x + 13, y + 12, accent);
  tft_.setTextSize(1);
  tft_.setTextColor(COLOR_WHITE, background);
  tft_.setCursor(x + 46, y + 13);
  tft_.print(LABELS[index]);
  tft_.setTextColor(selected ? accent : COLOR_MUTED, background);
  tft_.setCursor(x + 46, y + 28);
  tft_.print(SUBLABELS[index]);
}

void TftRenderer::drawHomeGlyph(
    uint8_t index, int16_t x, int16_t y, uint16_t color) {
  switch (index) {
    case 0:  // Data pulse
      tft_.drawFastHLine(x, y + 10, 5, color);
      tft_.drawLine(x + 5, y + 10, x + 8, y + 3, color);
      tft_.drawLine(x + 8, y + 3, x + 12, y + 17, color);
      tft_.drawLine(x + 12, y + 17, x + 16, y + 8, color);
      tft_.drawFastHLine(x + 16, y + 8, 5, color);
      break;
    case 1:  // CAN module
      tft_.drawRoundRect(x + 3, y + 4, 15, 13, 2, color);
      tft_.drawFastHLine(x + 6, y + 8, 9, color);
      tft_.drawFastHLine(x + 6, y + 12, 9, color);
      tft_.drawFastVLine(x, y + 8, 5, color);
      tft_.drawFastVLine(x + 21, y + 8, 5, color);
      break;
    case 2:
      drawIcon(UiIcon::Gps, x - 1, y - 2, color, UiTone::Neutral);
      break;
    case 3:  // Antenna mast with symmetric signal waves
      tft_.fillCircle(x + 11, y + 4, 2, color);
      tft_.drawFastVLine(x + 11, y + 7, 13, color);
      tft_.drawLine(x + 11, y + 13, x + 4, y + 21, color);
      tft_.drawLine(x + 11, y + 13, x + 18, y + 21, color);
      tft_.drawFastHLine(x + 3, y + 21, 17, color);
      tft_.drawLine(x + 6, y + 5, x + 3, y + 9, color);
      tft_.drawLine(x + 3, y + 9, x + 6, y + 13, color);
      tft_.drawLine(x + 16, y + 5, x + 19, y + 9, color);
      tft_.drawLine(x + 19, y + 9, x + 16, y + 13, color);
      break;
    case 4:  // Sliders
      tft_.drawFastHLine(x + 2, y + 5, 18, color);
      tft_.drawFastHLine(x + 2, y + 11, 18, color);
      tft_.drawFastHLine(x + 2, y + 17, 18, color);
      tft_.fillCircle(x + 8, y + 5, 2, color);
      tft_.fillCircle(x + 15, y + 11, 2, color);
      tft_.fillCircle(x + 6, y + 17, 2, color);
      break;
    case 5:  // Circular reboot arrow, not a power button
      tft_.drawLine(x + 18, y + 5, x + 13, y + 3, color);
      tft_.drawLine(x + 13, y + 3, x + 7, y + 4, color);
      tft_.drawLine(x + 7, y + 4, x + 3, y + 9, color);
      tft_.drawLine(x + 3, y + 9, x + 4, y + 15, color);
      tft_.drawLine(x + 4, y + 15, x + 9, y + 19, color);
      tft_.drawLine(x + 9, y + 19, x + 15, y + 18, color);
      tft_.drawLine(x + 15, y + 18, x + 19, y + 13, color);
      tft_.fillTriangle(x + 17, y + 2, x + 22, y + 4, x + 17, y + 8, color);
      break;
  }
}

void TftRenderer::drawDetail(const UiFrame& frame) {
  if (uiUsesWideDetail(frame)) {
    drawWideDetail(frame);
    return;
  }
  const uint8_t page = detailPage(frame);
  const uint8_t pageCount = uiDetailPageCount(frame);
  const uint8_t firstField = page * DETAIL_FIELDS_PER_PAGE;
  const int16_t cellWidth = 149;
  const int16_t cellHeight = 29;

  for (uint8_t local = 0; local < DETAIL_FIELDS_PER_PAGE; ++local) {
    const uint8_t index = firstField + local;
    if (index >= frame.fieldCount) break;
    const int16_t column = local & 1;
    const int16_t row = local >> 1;
    drawFieldCell(
        7 + column * 156,
        CONTENT_Y + 5 + row * cellHeight,
        cellWidth,
        frame.fields[index]);
  }

  if (frame.fieldCount == 0) {
    tft_.setTextColor(COLOR_MUTED, COLOR_BG);
    tft_.setTextSize(2);
    tft_.setCursor(107, 115);
    tft_.print(F("NO DATA"));
  }

  if (pageCount > 1) {
    tft_.setTextSize(1);
    tft_.fillRoundRect(266, CONTENT_Y + CONTENT_HEIGHT - 16, 47, 14,
                       4, COLOR_CYAN_DARK);
    tft_.setTextColor(COLOR_CYAN, COLOR_CYAN_DARK);
    tft_.setCursor(274, CONTENT_Y + CONTENT_HEIGHT - 12);
    tft_.print(String(page + 1) + "/" + String(pageCount));
  }

  drawDetailStatus(frame);
}

void TftRenderer::drawWideDetail(const UiFrame& frame) {
  const uint8_t page = detailPage(frame);
  const uint8_t pageCount = uiWidePageCount(frame);
  bool drewCard = false;
  for (uint8_t slot = 0; slot < UI_WIDE_CARDS_PER_PAGE; ++slot) {
    String label, value;
    if (!uiWideCardAt(frame, page * UI_WIDE_CARDS_PER_PAGE + slot,
                      label, value)) break;
    drawWideCard(slot, label, value);
    drewCard = true;
  }
  if (!drewCard) {
    tft_.setTextSize(2);
    tft_.setTextColor(COLOR_MUTED, COLOR_BG);
    tft_.setCursor(107, 115);
    tft_.print(F("NO DATA"));
  }
  if (pageCount > 1) {
    tft_.fillRoundRect(266, CONTENT_Y + CONTENT_HEIGHT - 16, 47, 14,
                       4, COLOR_CYAN_DARK);
    tft_.setTextSize(1);
    tft_.setTextColor(COLOR_CYAN, COLOR_CYAN_DARK);
    tft_.setCursor(274, CONTENT_Y + CONTENT_HEIGHT - 12);
    tft_.print(String(page + 1) + "/" + String(pageCount));
  }
  drawDetailStatus(frame);
}

void TftRenderer::drawWideCard(
    uint8_t slot, const String& label, const String& value) {
  const int16_t x = 7;
  const int16_t y = CONTENT_Y + 5 + slot * 48;
  tft_.fillRect(x, y, 306, 45, COLOR_BG);
  tft_.fillRoundRect(x, y, 306, 45, 5, COLOR_PANEL);
  tft_.drawRoundRect(x, y, 306, 45, 5, COLOR_LINE);
  tft_.fillRect(x + 6, y + 5, 2, 8, COLOR_CYAN);
  tft_.setTextSize(1);
  tft_.setTextColor(COLOR_CYAN, COLOR_PANEL);
  tft_.setCursor(x + 13, y + 4);
  tft_.print(clippedText(label, 48));
  tft_.setTextColor(COLOR_WHITE, COLOR_PANEL);
  String remaining = value;
  for (uint8_t line = 0; line < 3; ++line) {
    if (!remaining.length()) break;
    uint8_t take = remaining.length() > UI_WIDE_LINE_CHARS
        ? UI_WIDE_LINE_CHARS : remaining.length();
    if (remaining.length() > take && line < 2) {
      for (uint8_t space = take; space > 24; --space) {
        if (remaining[space] == ' ' &&
            remaining.length() - space - 1 <=
                (2 - line) * UI_WIDE_LINE_CHARS) {
          take = space;
          break;
        }
      }
    }
    tft_.setCursor(x + 9, y + 15 + line * 10);
    tft_.print(remaining.substring(0, take));
    remaining = remaining.substring(take);
    while (remaining.startsWith(" ")) remaining.remove(0, 1);
  }
}

void TftRenderer::updateMenuPartial(const UiFrame& frame) {
  if (isHome(frame)) {
    if (!isHome(cachedFrame_)) {
      tft_.fillRect(0, CONTENT_Y, SCREEN_WIDTH, CONTENT_HEIGHT, COLOR_BG);
      drawHome(frame);
    } else {
      for (uint8_t index = 0; index < 6; ++index) {
        if ((index == cachedFrame_.selectedIndex) !=
                (index == frame.selectedIndex) ||
            !sameItem(cachedFrame_.items[index], frame.items[index])) {
          drawHomeCard(frame, index);
        }
      }
    }
    return;
  }
  const uint8_t oldStart = menuStart(cachedFrame_);
  const uint8_t newStart = menuStart(frame);

  if (oldStart != newStart || cachedFrame_.itemCount != frame.itemCount) {
    tft_.fillRect(4, CONTENT_Y + 1, SCREEN_WIDTH - 8, CONTENT_HEIGHT - 2, COLOR_BG);
    drawMenu(frame);
    return;
  }

  const int16_t rowHeight = 25;
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
  if (uiUsesWideDetail(frame)) {
    const uint8_t page = detailPage(frame);
    for (uint8_t slot = 0; slot < UI_WIDE_CARDS_PER_PAGE; ++slot) {
      String oldLabel, oldValue, newLabel, newValue;
      const uint16_t card = page * UI_WIDE_CARDS_PER_PAGE + slot;
      const bool oldPresent = uiWideCardAt(
          cachedFrame_, card, oldLabel, oldValue);
      const bool newPresent = uiWideCardAt(
          frame, card, newLabel, newValue);
      if (newPresent && (!oldPresent || oldLabel != newLabel ||
                         oldValue != newValue)) {
        drawWideCard(slot, newLabel, newValue);
      } else if (oldPresent && !newPresent) {
        tft_.fillRect(7, CONTENT_Y + 5 + slot * 48, 306, 45, COLOR_BG);
      }
    }
    if (frame.status != cachedFrame_.status) drawDetailStatus(frame);
    return;
  }
  const uint8_t page = detailPage(frame);
  const uint8_t firstField = page * DETAIL_FIELDS_PER_PAGE;
  const int16_t cellWidth = 149;
  const int16_t cellHeight = 29;

  for (uint8_t local = 0; local < DETAIL_FIELDS_PER_PAGE; ++local) {
    const uint8_t index = firstField + local;
    if (index >= frame.fieldCount) break;

    if (!sameField(cachedFrame_.fields[index], frame.fields[index])) {
      const int16_t column = local & 1;
      const int16_t row = local >> 1;
      drawFieldCell(
          7 + column * 156,
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
  const bool paged = uiDetailPageCount(frame) > 1;
  tft_.fillRect(7, CONTENT_Y + CONTENT_HEIGHT - 15,
                paged ? 255 : 306, 14, COLOR_BG);
  if (!frame.status.length()) return;

  tft_.setTextSize(1);
  tft_.setTextColor(statusColor(frame.status), COLOR_BG);
  tft_.setCursor(9, CONTENT_Y + CONTENT_HEIGHT - 11);
  tft_.print(clippedText(frame.status, paged ? 41 : 49));
}

void TftRenderer::drawAlert(const UiFrame& frame) {
  const int16_t top = CONTENT_Y + 5;
  tft_.fillRoundRect(7, top, 306, 49, 6, COLOR_DARK_RED);
  tft_.drawRoundRect(7, top, 306, 49, 6, COLOR_RED);
  tft_.fillTriangle(17, top + 25, 27, top + 7, 37, top + 25, COLOR_RED);
  tft_.setTextSize(1);
  tft_.setTextColor(COLOR_BG, COLOR_RED);
  tft_.setCursor(25, top + 15);
  tft_.print('!');
  tft_.setTextColor(COLOR_RED, COLOR_DARK_RED);
  tft_.setCursor(47, top + 8);
  tft_.print(F("ATTENTION"));
  String note = cleanText(frame.subtitle.length() ? frame.subtitle : "System alert");
  uint8_t split = note.length() > 43 ? 43 : note.length();
  if (split == 43) {
    while (split > 28 && note[split] != ' ') --split;
  }
  tft_.setTextColor(COLOR_WHITE, COLOR_DARK_RED);
  tft_.setCursor(47, top + 20);
  tft_.print(note.substring(0, split));
  if (split < note.length()) {
    tft_.setCursor(47, top + 31);
    tft_.print(clippedText(note.substring(split + 1), 43));
  }

  const uint8_t visible = frame.itemCount < 3 ? frame.itemCount : 3;
  uint8_t start = frame.selectedIndex > 1 ? frame.selectedIndex - 1 : 0;
  if (start + visible > frame.itemCount) start = frame.itemCount - visible;
  for (uint8_t local = 0; local < visible; ++local) {
    const uint8_t index = start + local;
    const UiItem& item = frame.items[index];
    const bool selected = index == frame.selectedIndex;
    const int16_t y = CONTENT_Y + 59 + local * 27;
    const uint16_t background = selected ? COLOR_PANEL_ALT : COLOR_PANEL;
    const uint16_t accent = item.destructive || item.label.startsWith("[ERROR]")
        ? COLOR_RED : (item.label.startsWith("[WARNING]") ? COLOR_AMBER
        : COLOR_CYAN);
    tft_.fillRoundRect(8, y, 304, 25, 4, background);
    tft_.drawRoundRect(8, y, 304, 25, 4, selected ? accent : COLOR_LINE);
    tft_.setTextColor(selected ? accent : COLOR_WHITE, background);
    tft_.setCursor(17, y + 4);
    tft_.print(clippedText(item.label, 47));
    if (item.value.length()) {
      tft_.setTextColor(COLOR_MUTED, background);
      tft_.setCursor(17, y + 15);
      tft_.print(clippedText(item.value, 47));
    }
  }
  if (frame.itemCount > visible) {
    tft_.fillRoundRect(269, CONTENT_Y + CONTENT_HEIGHT - 17, 43, 13,
                       4, COLOR_PANEL_ALT);
    tft_.setTextColor(COLOR_AMBER, COLOR_PANEL_ALT);
    tft_.setCursor(276, CONTENT_Y + CONTENT_HEIGHT - 14);
    tft_.print(String(frame.selectedIndex + 1) + "/" + String(frame.itemCount));
  }
  if (frame.status.length()) {
    tft_.setTextColor(COLOR_AMBER, COLOR_BG);
    tft_.setCursor(9, CONTENT_Y + CONTENT_HEIGHT - 13);
    tft_.print(clippedText(frame.status, frame.itemCount > visible ? 40 : 49));
  }
}

void TftRenderer::drawFieldCell(
    int16_t x,
    int16_t y,
    int16_t width,
    const UiField& field) {
  tft_.fillRect(x, y, width, 27, COLOR_BG);
  tft_.fillRoundRect(x, y, width, 27, 4, COLOR_PANEL);
  tft_.drawRoundRect(x, y, width, 27, 4, COLOR_LINE);
  tft_.fillRect(x + 5, y + 4, 2, 7, COLOR_CYAN);

  tft_.setTextSize(1);
  tft_.setTextColor(COLOR_MUTED, COLOR_PANEL);
  tft_.setCursor(x + 11, y + 4);
  tft_.print(clippedText(field.label, 21));

  tft_.setTextColor(COLOR_WHITE, COLOR_PANEL);
  tft_.setCursor(x + 7, y + 16);
  tft_.print(clippedText(field.value, 22));
}

void TftRenderer::drawMenuRow(
    int16_t y,
    const UiItem& item,
    bool selected,
    uint8_t absoluteIndex) {
  const uint16_t background = selected ? COLOR_PANEL_ALT : COLOR_PANEL;
  const uint16_t border = selected ? (item.destructive ? COLOR_RED : COLOR_SELECT)
      : COLOR_LINE;
  const uint16_t text = !item.enabled
      ? COLOR_MUTED
      : (item.destructive ? COLOR_RED : (item.positive ? COLOR_GREEN : COLOR_WHITE));

  tft_.fillRect(7, y, 298, 23, COLOR_BG);
  tft_.fillRoundRect(7, y, 298, 23, 4, background);
  tft_.drawRoundRect(7, y, 298, 23, 4, border);

  if (selected) {
    tft_.fillRect(7, y + 5, 3, 13, item.destructive ? COLOR_RED : COLOR_SELECT);
  }

  tft_.setTextSize(1);
  tft_.fillRoundRect(15, y + 4, 19, 15, 3, selected ? COLOR_CYAN_DARK : COLOR_PANEL_ALT);
  tft_.setTextColor(selected ? COLOR_CYAN : COLOR_MUTED,
                    selected ? COLOR_CYAN_DARK : COLOR_PANEL_ALT);
  tft_.setCursor(18, y + 8);
  if (absoluteIndex + 1 < 10) tft_.print('0');
  tft_.print(absoluteIndex + 1);

  tft_.setTextColor(text, background);
  tft_.setCursor(41, item.value.length() ? y + 3 : y + 8);
  tft_.print(clippedText(item.label, 42));

  if (item.value.length()) {
    const bool off = item.value == "OFF";
    const bool on = item.value == "ON" || item.value == "ACTIVE";
    tft_.setTextColor(off ? COLOR_RED : (on ? COLOR_GREEN :
                      (selected ? COLOR_SELECT : COLOR_CYAN)), background);
    tft_.setCursor(41, y + 13);
    tft_.print(clippedText(item.value, 42));
  } else {
    tft_.setTextColor(selected ? COLOR_SELECT : COLOR_MUTED, background);
    tft_.setCursor(289, y + 8);
    tft_.print('>');
  }
}

void TftRenderer::drawScrollbar(uint8_t itemCount, uint8_t selectedIndex) {
  const int16_t trackY = CONTENT_Y + 6;
  const int16_t trackH = CONTENT_HEIGHT - 12;
  tft_.drawFastVLine(311, trackY, trackH, COLOR_LINE);

  const int16_t calculatedThumb = (trackH * MENU_VISIBLE_ROWS) / itemCount;
  const int16_t thumbH = calculatedThumb < 12 ? 12 : calculatedThumb;
  const int16_t travel = trackH - thumbH;
  const int16_t thumbY = trackY + (itemCount > 1
      ? (travel * selectedIndex) / (itemCount - 1)
      : 0);
  tft_.fillRoundRect(309, thumbY, 5, thumbH, 2, COLOR_SELECT);
}

String TftRenderer::cleanText(const String& value) const {
  return uiDisplayText(value);
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

uint16_t TftRenderer::toneColor(UiTone tone) const {
  switch (tone) {
    case UiTone::On:          return COLOR_GREEN;
    case UiTone::Off:         return COLOR_RED;
    case UiTone::Partial:     return COLOR_AMBER;
    case UiTone::Unavailable: return COLOR_MUTED;
    case UiTone::Neutral:     return COLOR_CYAN;
  }
  return COLOR_CYAN;
}

uint16_t TftRenderer::toneBackground(UiTone tone) const {
  switch (tone) {
    case UiTone::On:      return COLOR_DARK_GREEN;
    case UiTone::Off:     return COLOR_DARK_RED;
    case UiTone::Partial: return COLOR_DARK_AMBER;
    default:              return COLOR_PANEL;
  }
}

bool TftRenderer::sameScreenIdentity(const UiFrame& left, const UiFrame& right) const {
  if (uiUsesWideDetail(left) && uiUsesWideDetail(right)) {
    for (uint8_t index = 0; index < left.fieldCount &&
                            index < right.fieldCount; ++index) {
      if (uiWideCardCount(left.fields[index].value) !=
          uiWideCardCount(right.fields[index].value)) return false;
    }
  }
  return left.layout == right.layout &&
      left.title == right.title &&
      left.breadcrumb == right.breadcrumb &&
      left.itemCount == right.itemCount &&
      left.fieldCount == right.fieldCount;
}

uint8_t TftRenderer::detailPage(const UiFrame& frame) const {
  if (frame.layout == UiLayout::Menu || frame.layout == UiLayout::Tiles) {
    return 0;
  }

  const uint8_t pageCount = uiDetailPageCount(frame);
  if (pageCount <= 1) return 0;
  return frame.detailPage < pageCount ? frame.detailPage : pageCount - 1;
}

void TftRenderer::drawTiles(const UiFrame& frame) {
  for (uint8_t index = 0; index < frame.itemCount && index < 4; ++index) {
    const int16_t column = index & 1;
    const int16_t row = index >> 1;
    drawTile(
        7 + column * 156,
        CONTENT_Y + 5 + row * 79,
        150,
        74,
        frame.items[index],
        index == frame.selectedIndex);
  }
}

void TftRenderer::drawTile(
    int16_t x,
    int16_t y,
    int16_t width,
    int16_t height,
    const UiItem& item,
    bool selected) {
  const uint16_t background = selected ? COLOR_PANEL_ALT : COLOR_PANEL;
  const uint16_t accent = item.enabled ? toneColor(item.tone) : COLOR_MUTED;
  tft_.fillRoundRect(x, y, width, height, 6, background);
  tft_.drawRoundRect(x, y, width, height, 6,
                     selected ? COLOR_SELECT : COLOR_LINE);
  if (selected) {
    tft_.fillRect(x, y + 8, 3, height - 16, COLOR_SELECT);
  }
  tft_.fillRoundRect(x + 10, y + 10, 34, 34, 5, toneBackground(item.tone));
  drawIcon(item.icon, x + 15, y + 15, accent, item.tone);
  tft_.fillCircle(x + width - 13, y + 14, 3, accent);
  tft_.setTextSize(1);
  tft_.setTextColor(item.enabled ? COLOR_WHITE : COLOR_MUTED, background);
  tft_.setCursor(x + 52, y + 21);
  tft_.print(clippedText(item.label, 13));
  tft_.drawFastHLine(x + 10, y + 50, width - 20, COLOR_LINE);
  if (item.value.length()) {
    const String value = clippedText(item.value, 21);
    tft_.setTextColor(accent, background);
    tft_.setCursor(x + 11, y + 58);
    tft_.print(value);
  }
}

void TftRenderer::drawIcon(
    UiIcon icon,
    int16_t x,
    int16_t y,
    uint16_t color,
    UiTone tone) {
  const int16_t cx = x + 12;
  const int16_t cy = y + 12;
  switch (icon) {
    case UiIcon::StartStop:
      if (tone == UiTone::Off) tft_.fillRect(x + 5, y + 5, 14, 14, color);
      else tft_.fillTriangle(x + 6, y + 3, x + 6, y + 21, x + 21, cy, color);
      break;
    case UiIcon::Clock:
    case UiIcon::Timing:
      tft_.drawCircle(cx, cy, 10, color);
      tft_.drawLine(cx, cy, cx, y + 5, color);
      tft_.drawLine(cx, cy, x + 18, y + 15, color);
      break;
    case UiIcon::Sources:
      tft_.drawRect(x + 2, y + 3, 8, 8, color);
      tft_.drawRect(x + 14, y + 3, 8, 8, color);
      tft_.drawRect(x + 8, y + 15, 8, 8, color);
      tft_.drawLine(x + 6, y + 11, x + 11, y + 15, color);
      tft_.drawLine(x + 18, y + 11, x + 13, y + 15, color);
      break;
    case UiIcon::Status:
      tft_.drawCircle(cx, cy, 10, color);
      tft_.fillCircle(cx, y + 7, 1, color);
      tft_.drawFastVLine(cx, y + 11, 8, color);
      break;
    case UiIcon::Gps:
      tft_.drawCircle(cx, y + 9, 6, color);
      tft_.drawLine(x + 8, y + 13, cx, y + 22, color);
      tft_.drawLine(x + 16, y + 13, cx, y + 22, color);
      tft_.fillCircle(cx, y + 9, 2, color);
      break;
    case UiIcon::Obd:
      tft_.drawRect(x + 3, y + 7, 18, 11, color);
      tft_.drawLine(x + 6, y + 7, x + 8, y + 3, color);
      tft_.drawLine(x + 18, y + 7, x + 16, y + 3, color);
      tft_.fillCircle(x + 7, y + 19, 2, color);
      tft_.fillCircle(x + 17, y + 19, 2, color);
      break;
    case UiIcon::Engine:
      tft_.drawRect(x + 4, y + 6, 15, 13, color);
      tft_.drawFastHLine(x + 8, y + 3, 8, color);
      tft_.drawLine(x + 19, y + 10, x + 23, y + 8, color);
      tft_.drawLine(x + 4, y + 11, x + 1, y + 11, color);
      break;
    case UiIcon::Coolant:
      tft_.drawRoundRect(x + 9, y + 2, 6, 16, 3, color);
      tft_.fillCircle(cx, y + 19, 4, color);
      tft_.drawFastVLine(cx, y + 6, 11, color);
      break;
    case UiIcon::Throttle:
      tft_.drawLine(x + 5, y + 20, x + 16, y + 4, color);
      tft_.drawLine(x + 8, y + 21, x + 19, y + 5, color);
      tft_.drawFastHLine(x + 3, y + 21, 9, color);
      break;
    case UiIcon::Air:
      tft_.drawFastHLine(x + 2, y + 6, 17, color);
      tft_.drawFastHLine(x + 5, y + 12, 17, color);
      tft_.drawFastHLine(x + 2, y + 18, 15, color);
      tft_.fillCircle(x + 20, y + 6, 2, color);
      tft_.fillCircle(x + 3, y + 12, 2, color);
      tft_.fillCircle(x + 18, y + 18, 2, color);
      break;
    case UiIcon::Speed:
      tft_.drawCircle(cx, cy, 10, color);
      tft_.drawLine(cx, cy, x + 19, y + 7, color);
      tft_.fillCircle(cx, cy, 2, color);
      break;
    case UiIcon::Battery:
      tft_.drawRect(x + 3, y + 6, 18, 13, color);
      tft_.fillRect(x + 21, y + 10, 2, 5, color);
      tft_.drawFastHLine(x + 7, cy, 4, color);
      tft_.drawFastHLine(x + 14, cy, 4, color);
      tft_.drawFastVLine(x + 16, y + 10, 5, color);
      break;
    case UiIcon::Fuel:
      tft_.drawRect(x + 3, y + 3, 12, 19, color);
      tft_.drawRect(x + 5, y + 6, 8, 5, color);
      tft_.drawLine(x + 15, y + 6, x + 20, y + 9, color);
      tft_.drawLine(x + 20, y + 9, x + 20, y + 19, color);
      break;
    case UiIcon::None:
      break;
  }
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
      left.destructive == right.destructive &&
      left.positive == right.positive &&
      left.icon == right.icon &&
      left.tone == right.tone;
}

bool TftRenderer::sameField(const UiField& left, const UiField& right) const {
  return left.label == right.label && left.value == right.value;
}
