#include "UiModel.h"

void UiModel::publish(const UiFrame& frame, bool force) {
  if (!force && hasFrame_ && sameContent(frame_, frame)) {
    return;
  }

  frame_ = frame;
  frame_.generatedMs = millis();
  revision_++;
  hasFrame_ = true;
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
      left.detailPage != right.detailPage ||
      left.canGoBack != right.canGoBack) {
    return false;
  }

  for (uint8_t index = 0; index < left.itemCount; ++index) {
    if (left.items[index].label != right.items[index].label ||
        left.items[index].value != right.items[index].value ||
        left.items[index].enabled != right.items[index].enabled ||
        left.items[index].destructive != right.items[index].destructive ||
        left.items[index].positive != right.items[index].positive ||
        left.items[index].icon != right.items[index].icon ||
        left.items[index].tone != right.items[index].tone) {
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
