#pragma once

#include <Arduino.h>

constexpr uint8_t UI_MAX_ITEMS = 20;
constexpr uint8_t UI_MAX_FIELDS = 18;
constexpr uint8_t UI_DETAIL_FIELDS_PER_PAGE = 10;

enum class UiLayout : uint8_t {
  Menu,
  Tiles,
  Detail,
  Alert
};

enum class UiIcon : uint8_t {
  None, StartStop, Clock, Sources, Status, Gps, Obd, Engine,
  Coolant, Throttle, Timing, Air, Speed, Battery, Fuel
};

enum class UiTone : uint8_t { Neutral, On, Off, Partial, Unavailable };

struct UiItem {
  String label;
  String value;
  bool enabled = true;
  bool destructive = false;
  bool positive = false;
  UiIcon icon = UiIcon::None;
  UiTone tone = UiTone::Neutral;
};

struct UiField {
  String label;
  String value;
};

struct UiFrame {
  UiLayout layout = UiLayout::Menu;
  String title;
  String breadcrumb;
  String subtitle;
  String status;

  UiItem items[UI_MAX_ITEMS];
  uint8_t itemCount = 0;
  uint8_t selectedIndex = 0;

  UiField fields[UI_MAX_FIELDS];
  uint8_t fieldCount = 0;
  uint8_t detailPage = 0;

  bool canGoBack = false;
  uint32_t generatedMs = 0;
};

class UiModel {
public:
  void publish(const UiFrame& frame, bool force = false);

  const UiFrame& frame() const;
  uint32_t revision() const;

private:
  bool sameContent(const UiFrame& left, const UiFrame& right) const;
  UiFrame frame_;
  uint32_t revision_ = 0;
  bool hasFrame_ = false;
};
