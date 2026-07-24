#pragma once

#include <Arduino.h>

constexpr uint8_t UI_MAX_ITEMS = 20;
constexpr uint8_t UI_MAX_FIELDS = 18;

enum class UiLayout : uint8_t {
  Menu,
  Detail,
  Alert
};

struct UiItem {
  String label;
  String value;
  bool enabled = true;
  bool destructive = false;
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

  bool canGoBack = false;
  uint32_t generatedMs = 0;
};

class UiModel {
public:
  void begin(bool serialMirrorEnabled);
  void publish(const UiFrame& frame, bool force = false);

  void setSerialMirror(bool enabled);
  bool serialMirrorEnabled() const;

  const UiFrame& frame() const;
  uint32_t revision() const;

private:
  bool sameContent(const UiFrame& left, const UiFrame& right) const;
  void printFrameToSerial(const UiFrame& frame) const;
  const char* layoutLabel(UiLayout layout) const;

  UiFrame frame_;
  uint32_t revision_ = 0;
  bool serialMirrorEnabled_ = true;
  bool hasFrame_ = false;
};
