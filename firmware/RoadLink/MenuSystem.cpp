#include "MenuSystem.h"
#include "AppConfig.h"

MenuSystem::MenuSystem(
    UiModel& ui,
    CanService& can,
    ObdService& obd,
    GpsService& gps,
    A7670Service& sim,
    AppSettings& settings,
    SettingsStore& settingsStore,
    StartupDiagnostics& diagnostics,
    StreamingController& streaming)
  : ui_(ui),
    can_(can),
    obd_(obd),
    gps_(gps),
    sim_(sim),
    settings_(settings),
    settingsStore_(settingsStore),
    diagnostics_(diagnostics),
    streaming_(streaming) {
  navigation_[0].screen = ScreenId::MainMenu;
}

void MenuSystem::begin() {
  navigationDepth_ = 1;
  navigation_[0].screen = diagnostics_.hasErrors() && !diagnostics_.overridden()
      ? ScreenId::StartupErrors
      : ScreenId::MainMenu;
  navigation_[0].selection = navigation_[0].screen == ScreenId::StartupErrors
      ? diagnostics_.count()
      : 0;
  render(true);
}

void MenuSystem::handleInput(InputEvent event) {
  if (event == InputEvent::None) return;
  if (currentScreen() == ScreenId::RebootProgress) return;

  if (handleSimEditorInput(event)) {
    render(true);
    return;
  }

  if (handleStreamingIntervalInput(event)) {
    render(true);
    return;
  }

  if (event == InputEvent::Back) {
    goBack();
  } else if (event == InputEvent::RotateLeft) {
    rotate(-1);
  } else if (event == InputEvent::RotateRight) {
    rotate(1);
  } else if (event == InputEvent::Press) {
    press();
  }

  render(true);
}

void MenuSystem::update() {
  if (millis() - lastRenderMs_ >= settings_.uiRefreshMs) {
    render(false);
  }
}

ScreenId MenuSystem::currentScreen() const {
  return navigation_[navigationDepth_ - 1].screen;
}

uint8_t& MenuSystem::currentSelection() {
  return navigation_[navigationDepth_ - 1].selection;
}

const uint8_t& MenuSystem::currentSelection() const {
  return navigation_[navigationDepth_ - 1].selection;
}

void MenuSystem::pushScreen(ScreenId screen) {
  const ScreenId previous = currentScreen();

  if (navigationDepth_ < NAVIGATION_DEPTH) {
    navigation_[navigationDepth_].screen = screen;
    navigation_[navigationDepth_].selection = 0;
    navigation_[navigationDepth_].detailPage = 0;
    navigationDepth_++;
  } else {
    navigation_[NAVIGATION_DEPTH - 1].screen = screen;
    navigation_[NAVIGATION_DEPTH - 1].selection = 0;
    navigation_[NAVIGATION_DEPTH - 1].detailPage = 0;
  }

  onScreenChanged(previous, screen);
}

void MenuSystem::goBack() {
  if (navigationDepth_ <= 1) return;

  const ScreenId previous = currentScreen();
  navigationDepth_--;
  onScreenChanged(previous, currentScreen());
}

void MenuSystem::onScreenChanged(ScreenId previous, ScreenId current) {
  if (previous == ScreenId::ObdLiveData && current != ScreenId::ObdLiveData) {
    if (!streaming_.running()) obd_.setLivePolling(false);
  }

  if (current == ScreenId::ObdLiveData) {
    if (ensureObdTransmitMode()) {
      obd_.setLivePolling(true);
    }
  }
}

void MenuSystem::rotate(int8_t direction) {
  if (changeDetailPage(direction)) return;

  const uint8_t count = itemCount(currentScreen());
  if (count == 0) return;

  int16_t next = static_cast<int16_t>(currentSelection());
  for (uint8_t attempts = 0; attempts < count; ++attempts) {
    next += direction;
    if (next >= count) next = 0;
    if (next < 0) next = count - 1;

    if (itemEnabled(currentScreen(), static_cast<uint8_t>(next))) {
      currentSelection() = static_cast<uint8_t>(next);
      return;
    }
  }
}

bool MenuSystem::takeRebootRequest() {
  const bool requested = rebootRequested_;
  rebootRequested_ = false;
  return requested;
}

bool MenuSystem::changeDetailPage(int8_t direction) {
  const UiFrame frame = buildFrame(currentScreen());
  if (frame.layout == UiLayout::Menu || frame.layout == UiLayout::Tiles ||
      frame.fieldCount <= UI_DETAIL_FIELDS_PER_PAGE ||
      frame.itemCount > 1) {
    return false;
  }

  const uint8_t pageCount = static_cast<uint8_t>(
      (frame.fieldCount + UI_DETAIL_FIELDS_PER_PAGE - 1) /
      UI_DETAIL_FIELDS_PER_PAGE);
  int16_t next = static_cast<int16_t>(
      navigation_[navigationDepth_ - 1].detailPage) + direction;
  if (next < 0) next = pageCount - 1;
  if (next >= pageCount) next = 0;
  navigation_[navigationDepth_ - 1].detailPage = static_cast<uint8_t>(next);
  return true;
}

void MenuSystem::press() {
  const uint8_t count = itemCount(currentScreen());
  if (count == 0 || currentSelection() >= count) return;
  if (!itemEnabled(currentScreen(), currentSelection())) return;
  activateItem(currentScreen(), currentSelection());
}

uint8_t MenuSystem::itemCount(ScreenId screen) const {
  switch (screen) {
    case ScreenId::StartupErrors:      return diagnostics_.count() + 1;
    case ScreenId::StartupErrorDetail: return 1;
    case ScreenId::MainMenu:           return 6;
    case ScreenId::RebootConfirm:      return 2;
    case ScreenId::RebootProgress:     return 0;
    case ScreenId::StreamingMenu:      return 4;
    case ScreenId::StreamingSources:   return 3;
    case ScreenId::StreamingObdCategories: return 4;
    case ScreenId::StreamingEngine:    return 4;
    case ScreenId::StreamingAir:       return 2;
    case ScreenId::StreamingVehicle:   return 1;
    case ScreenId::StreamingPower:     return 2;
    case ScreenId::StreamingInterval:  return 0;
    case ScreenId::StreamingStatus:    return 1;
    case ScreenId::CanMenu:            return 7;
    case ScreenId::CanMonitorMenu:     return 3;
    case ScreenId::CanStatus:          return 2;
    case ScreenId::CanLiveFrame:       return 1;
    case ScreenId::CanIdBrowser:       return can_.statistics().uniqueIdCount + 1;
    case ScreenId::CanIdDetail:        return 1;
    case ScreenId::CanStatistics:      return 2;
    case ScreenId::ObdMenu:            return 8;
    case ScreenId::ObdStatus:          return 1;
    case ScreenId::ObdDiscovery:       return 2;
    case ScreenId::ObdLiveData:        return 3;
    case ScreenId::ObdSupportedPids:   return 2;
    case ScreenId::ObdDtcs:            return 2;
    case ScreenId::ObdClearConfirm:    return 2;
    case ScreenId::ObdVin:             return 2;
    case ScreenId::GpsMenu:            return 9;
    case ScreenId::GpsOverview:
    case ScreenId::GpsPosition:
    case ScreenId::GpsSignal:
    case ScreenId::GpsMotion:
    case ScreenId::GpsTime:
    case ScreenId::GpsNmeaStatistics:  return 1;
    case ScreenId::GpsRawNmea:         return 1;
    case ScreenId::SettingsMenu:       return 6;
    case ScreenId::SimConfiguration:   return 8;
    case ScreenId::SimStatus:          return 1;
    case ScreenId::SimErrors:          return 1;
    case ScreenId::SimIpEditor:
    case ScreenId::SimPortEditor:
    case ScreenId::SimKeyEditor:       return 0;
    case ScreenId::About:              return 1;
  }
  return 0;
}

String MenuSystem::itemLabel(ScreenId screen, uint8_t index) const {
  if (screen == ScreenId::StartupErrors) {
    if (index < diagnostics_.count()) {
      const ModuleError* error = diagnostics_.error(index);
      return error
          ? String(error->warning ? "[WARNING] " : "[ERROR] ") +
              moduleName(error->module)
          : "Unknown module";
    }
    return "OVERRIDE AND CONTINUE";
  }

  if (screen == ScreenId::MainMenu) {
    static const char* ITEMS[] = {
      "Streaming", "CAN Tools", "GPS Tools", "A7670SA / Cellular", "Settings", "Reboot"
    };
    return ITEMS[index];
  }

  if (screen == ScreenId::StreamingMenu) {
    static const char* ITEMS[] = {"START", "Interval", "Sources", "Status"};
    if (index == 0) return streaming_.running() ? "STOP" : "START";
    return ITEMS[index];
  }

  if (screen == ScreenId::StreamingSources) {
    static const char* ITEMS[] = {"GPS", "OBD-II", "OBD fields"};
    return ITEMS[index];
  }

  if (screen == ScreenId::StreamingObdCategories) {
    static const char* ITEMS[] = {"Engine", "Air / Intake", "Driving", "Power / Fuel"};
    return ITEMS[index];
  }

  if (screen == ScreenId::StreamingEngine) {
    static const char* ITEMS[] = {"RPM", "Coolant", "Throttle", "Timing"};
    return ITEMS[index];
  }
  if (screen == ScreenId::StreamingAir) {
    return index == 0 ? "Manifold" : "Intake temp";
  }
  if (screen == ScreenId::StreamingVehicle) return "Speed";
  if (screen == ScreenId::StreamingPower) {
    return index == 0 ? "ECU volts" : "Fuel level";
  }

  if (screen == ScreenId::CanMenu) {
    static const char* ITEMS[] = {
      "CAN Status", "Passive Monitor", "OBD-II Scanner", "Bus Statistics",
      "Clear CAN Statistics", "Retry CAN Controller", "< Back"
    };
    return ITEMS[index];
  }

  if (screen == ScreenId::CanMonitorMenu) {
    static const char* ITEMS[] = {"Live Frame", "ID Browser", "< Back"};
    return ITEMS[index];
  }

  if (screen == ScreenId::ObdMenu) {
    static const char* ITEMS[] = {
      "OBD Status", "Discover ECUs", "Live Data", "Supported PIDs",
      "Read DTCs", "Clear DTCs", "Read VIN", "< Back"
    };
    return ITEMS[index];
  }

  if (screen == ScreenId::GpsMenu) {
    static const char* ITEMS[] = {
      "Overview", "Position", "Fix / Signal", "Motion", "Time / Date",
      "NMEA Statistics", "Raw NMEA", "Reset GPS Statistics", "< Back"
    };
    return ITEMS[index];
  }

  if (screen == ScreenId::SettingsMenu) {
    static const char* ITEMS[] = {
      "CAN Bitrate", "CAN Operating Mode", "UI Refresh",
      "OBD Poll Interval", "About", "< Back"
    };
    return ITEMS[index];
  }

  if (screen == ScreenId::CanIdBrowser) {
    if (index >= can_.statistics().uniqueIdCount) return "< Back";
    const CanIdEntry* entry = can_.idEntry(index);
    return entry ? formatHexId(entry->id, entry->extended) : "Unavailable ID";
  }

  switch (screen) {
    case ScreenId::RebootConfirm:
      return index == 0 ? "CANCEL / Back" : "CONFIRM REBOOT";
    case ScreenId::StartupErrorDetail:
    case ScreenId::CanLiveFrame:
    case ScreenId::CanIdDetail:
    case ScreenId::ObdStatus:
    case ScreenId::GpsOverview:
    case ScreenId::GpsPosition:
    case ScreenId::GpsSignal:
    case ScreenId::GpsMotion:
    case ScreenId::GpsTime:
    case ScreenId::GpsNmeaStatistics:
    case ScreenId::About:
      return "< Back";
    case ScreenId::SimStatus:
    case ScreenId::SimErrors:
      return "< Back";

    case ScreenId::CanStatus:
      return index == 0 ? "Retry CAN Controller" : "< Back";
    case ScreenId::CanStatistics:
      return index == 0 ? "Clear Statistics" : "< Back";
    case ScreenId::ObdDiscovery:
      return index == 0 ? "Start ECU Discovery" : "< Back";
    case ScreenId::ObdLiveData:
      if (index == 0) {
        if (streaming_.running() && settings_.simSendObd) return "Streaming polling active";
        return obd_.livePollingEnabled() ? "Stop Live Polling" : "Start Live Polling";
      }
      return index == 1 ? "Read All Once" : "< Back";
    case ScreenId::ObdSupportedPids:
      return index == 0 ? "Scan Supported PIDs" : "< Back";
    case ScreenId::ObdDtcs:
      return index == 0 ? "Read Stored DTCs" : "< Back";
    case ScreenId::ObdClearConfirm:
      return index == 0 ? "CANCEL / Back" : "CONFIRM CLEAR DTCs";
    case ScreenId::ObdVin:
      return index == 0 ? "Read VIN" : "< Back";
    case ScreenId::GpsRawNmea:
      return "< Back";
    case ScreenId::SimConfiguration:
      switch (index) {
        case 0: return "Status / diagnostics";
        case 1: return "Errors / failure details";
        case 2: return sim_.enabled() ? "Disable modem checks" : "Enable modem checks";
        case 3: return "Receiver IP";
        case 4: return "Receiver port";
        case 5: return "Access key";
        case 6: return "Reconnect modem";
        default: return "< Back";
      }
    default:
      return "?";
  }
}

String MenuSystem::itemValue(ScreenId screen, uint8_t index) const {
  if (screen == ScreenId::StreamingMenu) {
    switch (index) {
      case 0: return streaming_.running() ? "RUNNING" : "READY";
      case 1: return streamIntervalLabel();
      case 2: return String((settings_.simSendGps ? 1 : 0) +
          (settings_.simSendObd ? 1 : 0)) + " / 2";
      case 3: return "REV " + String(settings_.streamConfigRevision);
    }
  }
  if (screen == ScreenId::StreamingSources) {
    if (index == 0) return settings_.simSendGps ? "ON" : "OFF";
    if (index == 1) return settings_.simSendObd ? "ON" : "OFF";
    return String(streaming_.selectedObdFieldCount()) + " selected";
  }
  if (screen == ScreenId::StreamingObdCategories) {
    static const uint16_t MASKS[] = {
      StreamField::RPM | StreamField::COOLANT | StreamField::THROTTLE | StreamField::TIMING,
      StreamField::MANIFOLD | StreamField::INTAKE_TEMP,
      StreamField::SPEED,
      StreamField::VOLTAGE | StreamField::FUEL_LEVEL
    };
    return String(selectedFieldCount(settings_.simObdFieldMask & MASKS[index])) +
        " / " + selectedFieldCount(MASKS[index]);
  }
  uint16_t streamField = 0;
  if (screen == ScreenId::StreamingEngine) {
    static const uint16_t FIELDS[] = {
      StreamField::RPM, StreamField::COOLANT, StreamField::THROTTLE, StreamField::TIMING};
    streamField = FIELDS[index];
  } else if (screen == ScreenId::StreamingAir) {
    streamField = index == 0 ? StreamField::MANIFOLD : StreamField::INTAKE_TEMP;
  } else if (screen == ScreenId::StreamingVehicle) {
    streamField = StreamField::SPEED;
  } else if (screen == ScreenId::StreamingPower) {
    streamField = index == 0 ? StreamField::VOLTAGE : StreamField::FUEL_LEVEL;
  }
  if (streamField) return settings_.simObdFieldMask & streamField ? "ON" : "OFF";

  if (screen == ScreenId::SettingsMenu) {
    switch (index) {
      case 0: return canBitrateLabel(settings_.canBitrate);
      case 1: return canModeLabel(settings_.canMode);
      case 2: return String(settings_.uiRefreshMs) + " ms";
      case 3: return String(settings_.obdPollMs) + " ms";
      default: return "";
    }
  }

  if (screen == ScreenId::SimConfiguration) {
      switch (index) {
        case 0: return sim_.stateLabel();
        case 1: return sim_.snapshot().failureCount
            ? String(sim_.snapshot().failureCount) + " RECORDED"
            : "NONE";
        case 2: return sim_.enabled() ? "ON" : "OFF";
        case 3: return ipLabel(settings_.simServerIp);
        case 4: return settings_.simServerPort
          ? String(settings_.simServerPort) : "NOT SET";
        case 5: {
        char key[7];
        snprintf(key, sizeof(key), "%06lu",
            static_cast<unsigned long>(settings_.simAccessKey));
        return key;
      }
      default: return "";
    }
  }

  if (screen == ScreenId::CanIdBrowser && index < can_.statistics().uniqueIdCount) {
    const CanIdEntry* entry = can_.idEntry(index);
    return entry ? String(entry->count) + " frames" : "";
  }

  if (screen == ScreenId::StartupErrors && index < diagnostics_.count()) {
    const ModuleError* error = diagnostics_.error(index);
    return error ? error->summary : "";
  }

  return "";
}

bool MenuSystem::itemEnabled(ScreenId screen, uint8_t index) const {
  if (screen == ScreenId::CanIdBrowser && index < can_.statistics().uniqueIdCount) {
    return can_.idEntry(index) != nullptr;
  }
  if (screen == ScreenId::SimConfiguration) {
    if (index == 6) return sim_.enabled();
  }
  if (screen == ScreenId::ObdLiveData && index == 0 &&
      streaming_.running() && settings_.simSendObd) return false;
  return true;
}

bool MenuSystem::itemDestructive(ScreenId screen, uint8_t index) const {
  return (screen == ScreenId::ObdClearConfirm && index == 1) ||
         (screen == ScreenId::CanMenu && index == 4) ||
         (screen == ScreenId::RebootConfirm && index == 1) ||
         (screen == ScreenId::StreamingMenu && index == 0 && streaming_.running());
}

UiIcon MenuSystem::itemIcon(ScreenId screen, uint8_t index) const {
  if (screen == ScreenId::StreamingMenu) {
    static const UiIcon ICONS[] = {
      UiIcon::StartStop, UiIcon::Clock, UiIcon::Sources, UiIcon::Status};
    return ICONS[index];
  }
  if (screen == ScreenId::StreamingSources) {
    static const UiIcon ICONS[] = {UiIcon::Gps, UiIcon::Obd, UiIcon::Sources};
    return ICONS[index];
  }
  if (screen == ScreenId::StreamingObdCategories) {
    static const UiIcon ICONS[] = {UiIcon::Engine, UiIcon::Air, UiIcon::Speed, UiIcon::Battery};
    return ICONS[index];
  }
  if (screen == ScreenId::StreamingEngine) {
    static const UiIcon ICONS[] = {UiIcon::Speed, UiIcon::Coolant, UiIcon::Throttle, UiIcon::Timing};
    return ICONS[index];
  }
  if (screen == ScreenId::StreamingAir) return index == 0 ? UiIcon::Air : UiIcon::Coolant;
  if (screen == ScreenId::StreamingVehicle) return UiIcon::Speed;
  if (screen == ScreenId::StreamingPower) return index == 0 ? UiIcon::Battery : UiIcon::Fuel;
  return UiIcon::None;
}

UiTone MenuSystem::itemTone(ScreenId screen, uint8_t index) const {
  if (screen == ScreenId::StreamingMenu) {
    if (index == 0) return streaming_.running() ? UiTone::Off : UiTone::On;
    if (index == 2) {
      const uint8_t selected = (settings_.simSendGps ? 1 : 0) +
          (settings_.simSendObd ? 1 : 0);
      return selected == 0 ? UiTone::Off : (selected == 2 ? UiTone::On : UiTone::Partial);
    }
    return UiTone::Neutral;
  }
  if (screen == ScreenId::StreamingSources) {
    if (index == 0) return settings_.simSendGps ? UiTone::On : UiTone::Off;
    if (index == 1) return settings_.simSendObd ? UiTone::On : UiTone::Off;
    return streaming_.selectedObdFieldCount() ? UiTone::Partial : UiTone::Off;
  }

  uint16_t mask = 0;
  if (screen == ScreenId::StreamingObdCategories) {
    static const uint16_t MASKS[] = {
      StreamField::RPM | StreamField::COOLANT | StreamField::THROTTLE | StreamField::TIMING,
      StreamField::MANIFOLD | StreamField::INTAKE_TEMP,
      StreamField::SPEED,
      StreamField::VOLTAGE | StreamField::FUEL_LEVEL};
    mask = MASKS[index];
    const uint8_t selected = selectedFieldCount(settings_.simObdFieldMask & mask);
    return selected == 0 ? UiTone::Off
        : (selected == selectedFieldCount(mask) ? UiTone::On : UiTone::Partial);
  }
  if (screen == ScreenId::StreamingEngine) {
    static const uint16_t FIELDS[] = {
      StreamField::RPM, StreamField::COOLANT, StreamField::THROTTLE, StreamField::TIMING};
    mask = FIELDS[index];
  } else if (screen == ScreenId::StreamingAir) {
    mask = index == 0 ? StreamField::MANIFOLD : StreamField::INTAKE_TEMP;
  } else if (screen == ScreenId::StreamingVehicle) {
    mask = StreamField::SPEED;
  } else if (screen == ScreenId::StreamingPower) {
    mask = index == 0 ? StreamField::VOLTAGE : StreamField::FUEL_LEVEL;
  }
  return mask && (settings_.simObdFieldMask & mask) ? UiTone::On
      : (mask ? UiTone::Off : UiTone::Neutral);
}

void MenuSystem::activateItem(ScreenId screen, uint8_t index) {
  if (screen == ScreenId::StartupErrors) {
    if (index < diagnostics_.count()) {
      selectedErrorIndex_ = index;
      pushScreen(ScreenId::StartupErrorDetail);
    } else {
      diagnostics_.overrideErrors();
      navigationDepth_ = 1;
      navigation_[0] = {ScreenId::MainMenu, 0};
    }
    return;
  }

  if (screen == ScreenId::MainMenu) {
    if (index == 0) pushScreen(ScreenId::StreamingMenu);
    else if (index == 1) pushScreen(ScreenId::CanMenu);
    else if (index == 2) pushScreen(ScreenId::GpsMenu);
    else if (index == 3) pushScreen(ScreenId::SimConfiguration);
    else if (index == 4) pushScreen(ScreenId::SettingsMenu);
    else pushScreen(ScreenId::RebootConfirm);
    return;
  }

  if (screen == ScreenId::StreamingMenu) {
    if (index == 0) {
      if (streaming_.running()) streaming_.stop();
      else streaming_.start();
    } else if (index == 1) beginStreamingIntervalEditor();
    else if (index == 2) pushScreen(ScreenId::StreamingSources);
    else pushScreen(ScreenId::StreamingStatus);
    return;
  }

  if (screen == ScreenId::StreamingSources) {
    if (index == 0) streaming_.setGpsEnabled(!settings_.simSendGps);
    else if (index == 1) streaming_.setObdEnabled(!settings_.simSendObd);
    else pushScreen(ScreenId::StreamingObdCategories);
    return;
  }

  if (screen == ScreenId::StreamingObdCategories) {
    static const ScreenId SCREENS[] = {
      ScreenId::StreamingEngine, ScreenId::StreamingAir,
      ScreenId::StreamingVehicle, ScreenId::StreamingPower};
    pushScreen(SCREENS[index]);
    return;
  }

  uint16_t streamField = 0;
  if (screen == ScreenId::StreamingEngine) {
    static const uint16_t FIELDS[] = {
      StreamField::RPM, StreamField::COOLANT, StreamField::THROTTLE, StreamField::TIMING};
    streamField = FIELDS[index];
  } else if (screen == ScreenId::StreamingAir) {
    streamField = index == 0 ? StreamField::MANIFOLD : StreamField::INTAKE_TEMP;
  } else if (screen == ScreenId::StreamingVehicle) {
    streamField = StreamField::SPEED;
  } else if (screen == ScreenId::StreamingPower) {
    streamField = index == 0 ? StreamField::VOLTAGE : StreamField::FUEL_LEVEL;
  }
  if (streamField) {
    streaming_.toggleObdField(streamField);
    return;
  }

  if (screen == ScreenId::CanMenu) {
    switch (index) {
      case 0: pushScreen(ScreenId::CanStatus); break;
      case 1: pushScreen(ScreenId::CanMonitorMenu); break;
      case 2: pushScreen(ScreenId::ObdMenu); break;
      case 3: pushScreen(ScreenId::CanStatistics); break;
      case 4: can_.clearStatistics(); break;
      case 5: retryCan(); break;
      case 6: goBack(); break;
    }
    return;
  }

  if (screen == ScreenId::CanMonitorMenu) {
    if (index == 0) pushScreen(ScreenId::CanLiveFrame);
    else if (index == 1) pushScreen(ScreenId::CanIdBrowser);
    else goBack();
    return;
  }

  if (screen == ScreenId::CanIdBrowser) {
    if (index >= can_.statistics().uniqueIdCount) goBack();
    else {
      selectedCanIdIndex_ = index;
      pushScreen(ScreenId::CanIdDetail);
    }
    return;
  }

  if (screen == ScreenId::ObdMenu) {
    switch (index) {
      case 0: pushScreen(ScreenId::ObdStatus); break;
      case 1: pushScreen(ScreenId::ObdDiscovery); break;
      case 2: pushScreen(ScreenId::ObdLiveData); break;
      case 3: pushScreen(ScreenId::ObdSupportedPids); break;
      case 4: pushScreen(ScreenId::ObdDtcs); break;
      case 5: pushScreen(ScreenId::ObdClearConfirm); break;
      case 6: pushScreen(ScreenId::ObdVin); break;
      case 7: goBack(); break;
    }
    return;
  }

  if (screen == ScreenId::GpsMenu) {
    switch (index) {
      case 0: pushScreen(ScreenId::GpsOverview); break;
      case 1: pushScreen(ScreenId::GpsPosition); break;
      case 2: pushScreen(ScreenId::GpsSignal); break;
      case 3: pushScreen(ScreenId::GpsMotion); break;
      case 4: pushScreen(ScreenId::GpsTime); break;
      case 5: pushScreen(ScreenId::GpsNmeaStatistics); break;
      case 6: pushScreen(ScreenId::GpsRawNmea); break;
      case 7: gps_.resetStatistics(); break;
      case 8: goBack(); break;
    }
    return;
  }

  if (screen == ScreenId::SettingsMenu) {
    switch (index) {
      case 0: cycleCanBitrate(); break;
      case 1: toggleCanMode(); break;
      case 2: cycleUiRefresh(); break;
      case 3: cycleObdPoll(); break;
      case 4: pushScreen(ScreenId::About); break;
      case 5: goBack(); break;
    }
    return;
  }

  switch (screen) {
    case ScreenId::RebootConfirm:
      if (index == 0) goBack();
      else {
        obd_.cancelOperation();
        obd_.setLivePolling(false);
        streaming_.stop();
        rebootRequested_ = true;
        pushScreen(ScreenId::RebootProgress);
      }
      break;
    case ScreenId::StartupErrorDetail:
    case ScreenId::CanLiveFrame:
    case ScreenId::CanIdDetail:
    case ScreenId::ObdStatus:
    case ScreenId::GpsOverview:
    case ScreenId::GpsPosition:
    case ScreenId::GpsSignal:
    case ScreenId::GpsMotion:
    case ScreenId::GpsTime:
    case ScreenId::GpsNmeaStatistics:
    case ScreenId::About:
    case ScreenId::SimStatus:
    case ScreenId::SimErrors:
    case ScreenId::StreamingStatus:
      goBack();
      break;

    case ScreenId::CanStatus:
      index == 0 ? retryCan() : goBack();
      break;
    case ScreenId::CanStatistics:
      if (index == 0) can_.clearStatistics(); else goBack();
      break;
    case ScreenId::ObdDiscovery:
      if (index == 0) {
        if (ensureObdTransmitMode()) obd_.startDiscovery();
      } else goBack();
      break;
    case ScreenId::ObdLiveData:
      if (index == 0) {
        if (obd_.livePollingEnabled()) obd_.setLivePolling(false);
        else if (ensureObdTransmitMode()) obd_.setLivePolling(true);
      } else if (index == 1) {
        if (ensureObdTransmitMode()) obd_.requestLiveDataOnce();
      } else goBack();
      break;
    case ScreenId::ObdSupportedPids:
      if (index == 0) {
        if (ensureObdTransmitMode()) obd_.startSupportedPidScan();
      } else goBack();
      break;
    case ScreenId::ObdDtcs:
      if (index == 0) {
        if (ensureObdTransmitMode()) obd_.startReadDtcs();
      } else goBack();
      break;
    case ScreenId::ObdClearConfirm:
      if (index == 0) goBack();
      else if (ensureObdTransmitMode()) {
        obd_.startClearDtcs();
        goBack();
      }
      break;
    case ScreenId::ObdVin:
      if (index == 0) {
        if (ensureObdTransmitMode()) obd_.startReadVin();
      } else goBack();
      break;
    case ScreenId::GpsRawNmea:
      goBack();
      break;
    case ScreenId::SimConfiguration:
      switch (index) {
        case 0:
          pushScreen(ScreenId::SimStatus);
          break;
        case 1:
          pushScreen(ScreenId::SimErrors);
          break;
        case 2:
          if (sim_.enabled()) streaming_.stop();
          settings_.simEnabled = !settings_.simEnabled;
          sim_.setEnabled(settings_.simEnabled);
          settingsStore_.saveSim(settings_);
          break;
        case 3:
          beginSimEditor(ScreenId::SimIpEditor);
          break;
        case 4:
          beginSimEditor(ScreenId::SimPortEditor);
          break;
        case 5:
          beginSimEditor(ScreenId::SimKeyEditor);
          break;
        case 6:
          sim_.requestReconnect();
          break;
        case 7:
          goBack();
          break;
      }
      break;
    default:
      break;
  }
}

void MenuSystem::render(bool force) {
  UiFrame frame = buildFrame(currentScreen());
  const uint8_t count = frame.itemCount;
  if (count > 0 && currentSelection() >= count) currentSelection() = count - 1;
  frame.selectedIndex = count > 0 ? currentSelection() : 0;
  ui_.publish(frame, force);
  lastRenderMs_ = millis();
}

UiFrame MenuSystem::buildFrame(ScreenId screen) const {
  UiFrame frame;
  frame.layout = layoutFor(screen);
  frame.title = titleFor(screen);
  frame.breadcrumb = breadcrumbFor(screen);
  frame.canGoBack = navigationDepth_ > 1;
  frame.detailPage = navigation_[navigationDepth_ - 1].detailPage;

  const uint8_t count = itemCount(screen);
  for (uint8_t index = 0; index < count; ++index) {
    addItem(
        frame,
        itemLabel(screen, index),
        itemValue(screen, index),
        itemEnabled(screen, index),
        itemDestructive(screen, index),
        itemIcon(screen, index),
        itemTone(screen, index));
    frame.items[frame.itemCount - 1].positive =
        screen == ScreenId::StreamingMenu && index == 0 && !streaming_.running();
  }

  switch (screen) {
    case ScreenId::RebootConfirm:
      frame.subtitle = "Restart ESP32 and request A7670SA reset over UART.";
      frame.status = "Settings kept. External module power stays on.";
      break;
    case ScreenId::RebootProgress:
      frame.canGoBack = false;
      addField(frame, "Modem", sim_.stateLabel());
      addField(frame, "ESP32", "Restart after modem reset");
      addField(frame, "Telemetry", "Stopped until START");
      frame.status = "Please wait. External modules remain powered.";
      break;
    case ScreenId::StreamingInterval: fillStreamingInterval(frame); break;
    case ScreenId::StreamingStatus:   fillStreamingStatus(frame); break;
    case ScreenId::StartupErrorDetail: fillStartupErrorDetail(frame); break;
    case ScreenId::CanStatus:          fillCanStatus(frame); break;
    case ScreenId::CanLiveFrame:       fillCanLiveFrame(frame); break;
    case ScreenId::CanIdDetail:        fillCanIdDetail(frame); break;
    case ScreenId::CanStatistics:      fillCanStatistics(frame); break;
    case ScreenId::ObdStatus:          fillObdStatus(frame); break;
    case ScreenId::ObdDiscovery:       fillObdDiscovery(frame); break;
    case ScreenId::ObdLiveData:        fillObdLiveData(frame); break;
    case ScreenId::ObdSupportedPids:   fillObdSupportedPids(frame); break;
    case ScreenId::ObdDtcs:            fillObdDtcs(frame); break;
    case ScreenId::ObdClearConfirm:
      frame.layout = UiLayout::Alert;
      frame.subtitle = "Clearing DTCs may erase freeze-frame data and reset readiness monitors.";
      frame.status = "Select CONFIRM only with the vehicle safely stationary.";
      break;
    case ScreenId::ObdVin:             fillObdVin(frame); break;
    case ScreenId::GpsOverview:        fillGpsOverview(frame); break;
    case ScreenId::GpsPosition:        fillGpsPosition(frame); break;
    case ScreenId::GpsSignal:          fillGpsSignal(frame); break;
    case ScreenId::GpsMotion:          fillGpsMotion(frame); break;
    case ScreenId::GpsTime:            fillGpsTime(frame); break;
    case ScreenId::GpsNmeaStatistics:  fillGpsNmeaStatistics(frame); break;
    case ScreenId::GpsRawNmea:         fillGpsRawNmea(frame); break;
    case ScreenId::SimStatus:          fillSimConfiguration(frame); break;
    case ScreenId::SimErrors:          fillSimErrors(frame); break;
    case ScreenId::SimIpEditor:
    case ScreenId::SimPortEditor:
    case ScreenId::SimKeyEditor:       fillSimEditor(frame, screen); break;
    case ScreenId::About:              fillAbout(frame); break;
    default:                            break;
  }

  if (screen == ScreenId::StartupErrors) {
    frame.layout = UiLayout::Alert;
    frame.subtitle = diagnostics_.hasFatalErrors()
        ? "Startup errors or module warnings detected."
        : "Optional module warnings detected.";
    frame.status = "Inspect a module or choose OVERRIDE AND CONTINUE.";
  } else if (screen == ScreenId::CanIdBrowser) {
    frame.subtitle = "Discovered CAN identifiers";
    frame.status = String(can_.statistics().uniqueIdCount) + " unique IDs";
  } else if (screen == ScreenId::MainMenu) {
    frame.subtitle = "Rotary tree navigation • Press to select";
  } else if (screen == ScreenId::StreamingMenu) {
    frame.subtitle = "Selected data is collected only after START";
    frame.status = streaming_.statusMessage();
  } else if (screen == ScreenId::StreamingSources) {
    frame.subtitle = "Modules keep their field choices when disabled";
    frame.status = "Device health is always included in heartbeat.";
  } else if (screen == ScreenId::StreamingObdCategories ||
      screen == ScreenId::StreamingEngine ||
      screen == ScreenId::StreamingAir ||
      screen == ScreenId::StreamingVehicle ||
      screen == ScreenId::StreamingPower) {
    frame.subtitle = "Green: selected  Red: off  Hold: back";
  } else if (screen == ScreenId::SimConfiguration) {
    frame.status = !sim_.enabled() ? "Checks paused; modem still has power."
        : (!sim_.endpointConfigured() ? "Set IP and port for telemetry and heartbeat."
        : (!sim_.ready() ? "Waiting for modem checks."
        : "Modem ready. Streaming controls are on the main menu."));
  }

  return frame;
}

String MenuSystem::titleFor(ScreenId screen) const {
  switch (screen) {
    case ScreenId::StartupErrors:      return "Startup Diagnostics";
    case ScreenId::StartupErrorDetail: return "Error Details";
    case ScreenId::MainMenu:           return "Scan-Track-Log";
    case ScreenId::RebootConfirm:      return "Reboot";
    case ScreenId::RebootProgress:     return "Rebooting";
    case ScreenId::StreamingMenu:      return "Streaming";
    case ScreenId::StreamingSources:   return "Stream Sources";
    case ScreenId::StreamingObdCategories: return "OBD-II Fields";
    case ScreenId::StreamingEngine:    return "Engine";
    case ScreenId::StreamingAir:       return "Air / Intake";
    case ScreenId::StreamingVehicle:   return "Driving";
    case ScreenId::StreamingPower:     return "Power / Fuel";
    case ScreenId::StreamingInterval:  return "Send Interval";
    case ScreenId::StreamingStatus:    return "Streaming Status";
    case ScreenId::CanMenu:            return "CAN Tools";
    case ScreenId::CanMonitorMenu:     return "Passive CAN Monitor";
    case ScreenId::CanStatus:          return "CAN Status";
    case ScreenId::CanLiveFrame:       return "Latest CAN Frame";
    case ScreenId::CanIdBrowser:       return "CAN ID Browser";
    case ScreenId::CanIdDetail:        return "CAN ID Details";
    case ScreenId::CanStatistics:      return "CAN Statistics";
    case ScreenId::ObdMenu:            return "OBD-II Scanner";
    case ScreenId::ObdStatus:          return "OBD-II Status";
    case ScreenId::ObdDiscovery:       return "ECU Discovery";
    case ScreenId::ObdLiveData:        return "OBD-II Live Data";
    case ScreenId::ObdSupportedPids:   return "Supported PIDs";
    case ScreenId::ObdDtcs:            return "Diagnostic Trouble Codes";
    case ScreenId::ObdClearConfirm:    return "Clear DTCs";
    case ScreenId::ObdVin:             return "Vehicle VIN";
    case ScreenId::GpsMenu:            return "GPS Tools";
    case ScreenId::GpsOverview:        return "GPS Overview";
    case ScreenId::GpsPosition:        return "GPS Position";
    case ScreenId::GpsSignal:          return "GPS Fix / Signal";
    case ScreenId::GpsMotion:          return "GPS Motion";
    case ScreenId::GpsTime:            return "GPS Time / Date";
    case ScreenId::GpsNmeaStatistics:  return "NMEA Statistics";
    case ScreenId::GpsRawNmea:         return "Raw NMEA";
    case ScreenId::SettingsMenu:       return "Settings";
    case ScreenId::SimConfiguration:   return "A7670SA Configuration";
    case ScreenId::SimStatus:          return "A7670SA Status";
    case ScreenId::SimErrors:          return "A7670SA Errors";
    case ScreenId::SimIpEditor:        return "Receiver IP";
    case ScreenId::SimPortEditor:      return "Receiver Port";
    case ScreenId::SimKeyEditor:       return "Access Key";
    case ScreenId::About:              return "About";
  }
  return "RoadLink";
}

String MenuSystem::breadcrumbFor(ScreenId screen) const {
  (void)screen;
  String path;
  for (uint8_t index = 0; index < navigationDepth_; ++index) {
    if (index) path += " / ";
    path += titleFor(navigation_[index].screen);
  }
  return path;
}

UiLayout MenuSystem::layoutFor(ScreenId screen) const {
  switch (screen) {
    case ScreenId::StartupErrors:
    case ScreenId::ObdClearConfirm:
    case ScreenId::RebootConfirm:
      return UiLayout::Alert;
    case ScreenId::StreamingMenu:
    case ScreenId::StreamingSources:
    case ScreenId::StreamingObdCategories:
    case ScreenId::StreamingEngine:
    case ScreenId::StreamingAir:
    case ScreenId::StreamingVehicle:
    case ScreenId::StreamingPower:
      return UiLayout::Tiles;
    case ScreenId::MainMenu:
    case ScreenId::CanMenu:
    case ScreenId::CanMonitorMenu:
    case ScreenId::CanIdBrowser:
    case ScreenId::ObdMenu:
    case ScreenId::GpsMenu:
    case ScreenId::SettingsMenu:
    case ScreenId::SimConfiguration:
      return UiLayout::Menu;
    default:
      return UiLayout::Detail;
  }
}

void MenuSystem::addItem(
    UiFrame& frame,
    const String& label,
    const String& value,
    bool enabled,
    bool destructive,
    UiIcon icon,
    UiTone tone) const {
  if (frame.itemCount >= UI_MAX_ITEMS) return;
  UiItem& item = frame.items[frame.itemCount++];
  item.label = label;
  item.value = value;
  item.enabled = enabled;
  item.destructive = destructive;
  item.icon = icon;
  item.tone = tone;
}

void MenuSystem::addField(UiFrame& frame, const String& label, const String& value) const {
  if (frame.fieldCount >= UI_MAX_FIELDS) return;
  frame.fields[frame.fieldCount].label = label;
  frame.fields[frame.fieldCount].value = value;
  frame.fieldCount++;
}

void MenuSystem::fillStartupErrorDetail(UiFrame& frame) const {
  const ModuleError* error = diagnostics_.error(selectedErrorIndex_);
  if (!error) {
    frame.status = "Error entry unavailable";
    return;
  }
  addField(frame, "Module", moduleName(error->module));
  addField(frame, "Severity", error->warning ? "Warning" : "Error");
  addField(frame, "Summary", error->summary);
  addField(frame, "Primary code", String(error->primaryCode));
  addField(frame, "Secondary code", String(error->secondaryCode));
}

void MenuSystem::fillCanStatus(UiFrame& frame) const {
  const CanStatistics& stats = can_.statistics();
  addField(frame, "Controller", can_.initialized() ? "MCP2515 online" : "MCP2515 offline");
  addField(frame, "Bitrate", canBitrateLabel(can_.bitrate()));
  addField(frame, "Operating mode", canModeLabel(can_.operatingMode()));
  addField(frame, "Bus activity", can_.busActive() ? "Active" : "Idle / no frames");
  addField(frame, "Receive rate", String(stats.framesPerSecond) + " frames/s");
  addField(frame, "Init result", String(can_.initializationResult()));
  addField(frame, "Controller error", String(can_.controllerError()));
  addField(frame, "Last TX result", String(can_.lastTransmitResult()));
  frame.status = can_.canTransmit()
      ? "Bidirectional CAN transmission enabled"
      : "Listen-only: transmission disabled";
}

void MenuSystem::fillCanLiveFrame(UiFrame& frame) const {
  const CanFrameSnapshot& canFrame = can_.lastFrame();
  if (!canFrame.valid) {
    frame.status = "Waiting for CAN traffic";
    return;
  }
  addField(frame, "Identifier", formatHexId(canFrame.id, canFrame.extended));
  addField(frame, "Frame type", canFrame.remote ? "Remote request" : "Data frame");
  addField(frame, "DLC", String(canFrame.dlc));
  addField(frame, "Data", formatData(canFrame.data, canFrame.dlc));
  addField(frame, "Age", ageLabel(millis() - canFrame.timestampMs));
  addField(frame, "Bus rate", String(can_.statistics().framesPerSecond) + " frames/s");
}

void MenuSystem::fillCanIdDetail(UiFrame& frame) const {
  const CanIdEntry* entry = can_.idEntry(selectedCanIdIndex_);
  if (!entry) {
    frame.status = "Selected identifier is unavailable";
    return;
  }
  addField(frame, "Identifier", formatHexId(entry->id, entry->extended));
  addField(frame, "Observed frames", String(entry->count));
  addField(frame, "Last DLC", String(entry->dlc));
  addField(frame, "Last data", formatData(entry->lastData, entry->dlc));
  addField(frame, "Last seen", ageLabel(millis() - entry->lastSeenMs));
}

void MenuSystem::fillCanStatistics(UiFrame& frame) const {
  const CanStatistics& stats = can_.statistics();
  addField(frame, "Received frames", String(stats.totalFrames));
  addField(frame, "Current rate", String(stats.framesPerSecond) + " frames/s");
  addField(frame, "Peak rate", String(stats.peakFramesPerSecond) + " frames/s");
  addField(frame, "Unique IDs", String(stats.uniqueIdCount));
  addField(frame, "Standard frames", String(stats.standardFrames));
  addField(frame, "Extended frames", String(stats.extendedFrames));
  addField(frame, "Remote frames", String(stats.remoteFrames));
  addField(frame, "Transmitted frames", String(stats.transmittedFrames));
  addField(frame, "TX errors", String(stats.transmitErrors));
  addField(frame, "ID table overflow", String(stats.tableOverflowCount));
  addField(frame, "Diagnostic queue overflow", String(stats.diagnosticQueueOverflows));
}

void MenuSystem::fillObdStatus(UiFrame& frame) const {
  const ObdStatistics& stats = obd_.statistics();
  addField(frame, "CAN transmit", can_.canTransmit() ? "Ready" : "Not ready");
  addField(frame, "Operation", ObdService::operationLabel(obd_.operation()));
  addField(frame, "Result", ObdService::resultLabel(obd_.result()));
  addField(frame, "ECUs discovered", String(obd_.ecuCount()));
  addField(frame, "Last response ID", obd_.lastResponseId() ? formatHexId(obd_.lastResponseId()) : "None");
  addField(frame, "Requests sent", String(stats.requestsSent));
  addField(frame, "Responses", String(stats.responsesReceived));
  addField(frame, "Timeouts", String(stats.timeouts));
  addField(frame, "Negative responses", String(stats.negativeResponses));
  addField(frame, "Flow-control frames", String(stats.flowControlFrames));
  frame.status = obd_.statusMessage();
}

void MenuSystem::fillObdDiscovery(UiFrame& frame) const {
  addField(frame, "Operation", ObdService::operationLabel(obd_.operation()));
  addField(frame, "Result", ObdService::resultLabel(obd_.result()));
  addField(frame, "Responding ECUs", String(obd_.ecuCount()));
  for (uint8_t index = 0; index < obd_.ecuCount(); ++index) {
    const ObdEcuInfo* ecu = obd_.ecu(index);
    if (!ecu) continue;
    addField(
        frame,
        "ECU " + String(index + 1),
        "RX " + formatHexId(ecu->responseId) + " / TX " + formatHexId(ecu->requestId));
  }
  frame.status = obd_.statusMessage();
}

void MenuSystem::fillObdLiveData(UiFrame& frame) const {
  const ObdLiveData& data = obd_.liveData();
  addField(frame, "Engine RPM", floatValue(data.rpmValid, data.rpm, 0, "rpm"));
  addField(frame, "Vehicle speed", floatValue(data.speedValid, data.speedKmh, 0, "km/h"));
  addField(frame, "Coolant", floatValue(data.coolantValid, data.coolantC, 0, "°C"));
  addField(frame, "Throttle", floatValue(data.throttleValid, data.throttlePercent, 1, "%"));
  addField(frame, "Manifold pressure", floatValue(data.mapValid, data.mapKpa, 0, "kPa"));
  addField(frame, "Intake temperature", floatValue(data.intakeTempValid, data.intakeTempC, 0, "°C"));
  addField(frame, "Ignition timing", floatValue(data.timingValid, data.timingDegrees, 1, "°"));
  addField(frame, "Module voltage", floatValue(data.voltageValid, data.moduleVoltage, 2, "V"));
  addField(frame, "Fuel level", floatValue(data.fuelLevelValid, data.fuelLevelPercent, 1, "%"));
  addField(frame, "Last update", data.lastUpdateMs ? ageLabel(millis() - data.lastUpdateMs) : "No data");
  frame.status = obd_.statusMessage();
}

void MenuSystem::fillObdSupportedPids(UiFrame& frame) const {
  const ObdEcuInfo* ecu = obd_.primaryEcu();
  addField(frame, "Primary ECU", ecu ? formatHexId(ecu->responseId) : "Not discovered");
  addField(frame, "PIDs 01-20", supportedMaskLabel(ecu, 0));
  addField(frame, "PIDs 21-40", supportedMaskLabel(ecu, 1));
  addField(frame, "PIDs 41-60", supportedMaskLabel(ecu, 2));
  addField(frame, "PIDs 61-80", supportedMaskLabel(ecu, 3));
  frame.status = obd_.statusMessage();
}

void MenuSystem::fillObdDtcs(UiFrame& frame) const {
  addField(frame, "DTC count", String(obd_.dtcCount()));
  for (uint8_t index = 0; index < obd_.dtcCount(); ++index) {
    const ObdDtc* dtc = obd_.dtc(index);
    if (dtc && dtc->valid) {
      addField(frame, "DTC " + String(index + 1), String(dtc->code));
    }
  }
  frame.status = obd_.statusMessage();
}

void MenuSystem::fillObdVin(UiFrame& frame) const {
  addField(frame, "VIN", obd_.vin().length() ? obd_.vin() : "Not read");
  addField(frame, "Last response ID", obd_.lastResponseId() ? formatHexId(obd_.lastResponseId()) : "None");
  frame.status = obd_.statusMessage();
}

void MenuSystem::fillGpsOverview(UiFrame& frame) const {
  const GpsSnapshot& gps = gps_.snapshot();
  const GpsStatistics& stats = gps_.statistics();
  addField(frame, "Fix", gps_.hasFix() ? "Valid" : "No fix");
  addField(frame, "Satellites", gps.satellites.length() ? gps.satellites : "0");
  addField(frame, "HDOP", gps.hdop.length() ? gps.hdop : "--");
  addField(frame, "Altitude", gps.altitudeMeters.length() ? gps.altitudeMeters + " m" : "--");
  addField(frame, "NMEA age", ageLabel(gps_.lastSentenceAgeMs()));
  addField(frame, "PPS pulses", String(stats.ppsCount));
}

void MenuSystem::fillGpsPosition(UiFrame& frame) const {
  const GpsSnapshot& gps = gps_.snapshot();
  addField(frame, "Position valid", gps.positionValid ? "Yes" : "No");
  addField(frame, "Latitude", gps.positionValid ? String(gps.latitudeDecimal, 6) : "--");
  addField(frame, "Longitude", gps.positionValid ? String(gps.longitudeDecimal, 6) : "--");
  addField(frame, "Google Maps", gps.positionValid
      ? String(gps.latitudeDecimal, 6) + ", " + String(gps.longitudeDecimal, 6)
      : "--");
  addField(frame, "Altitude", gps.altitudeMeters.length() ? gps.altitudeMeters + " m" : "--");
}

void MenuSystem::fillGpsSignal(UiFrame& frame) const {
  const GpsSnapshot& gps = gps_.snapshot();
  const GpsStatistics& stats = gps_.statistics();
  addField(frame, "Fix", gps_.hasFix() ? "Valid" : "No fix");
  addField(frame, "Fix quality", gps.fixQuality.length() ? gps.fixQuality : "--");
  addField(frame, "RMC status", gps.rmcStatus.length() ? gps.rmcStatus : "--");
  addField(frame, "Satellites", gps.satellites.length() ? gps.satellites : "0");
  addField(frame, "HDOP", gps.hdop.length() ? gps.hdop : "--");
  addField(frame, "PPS count", String(stats.ppsCount));
  addField(frame, "Last PPS", stats.lastPpsMs ? ageLabel(millis() - stats.lastPpsMs) : "No pulse");
}

void MenuSystem::fillGpsMotion(UiFrame& frame) const {
  const GpsSnapshot& gps = gps_.snapshot();
  const float speedKmh = gps.speedKnots.length() ? gps.speedKnots.toFloat() * 1.852f : 0.0f;
  addField(frame, "Speed", gps.speedKnots.length() ? String(speedKmh, 1) + " km/h" : "--");
  addField(frame, "Speed", gps.speedKnots.length() ? gps.speedKnots + " knots" : "--");
  addField(frame, "Course", gps.courseDegrees.length() ? gps.courseDegrees + "°" : "--");
}

void MenuSystem::fillGpsTime(UiFrame& frame) const {
  const GpsSnapshot& gps = gps_.snapshot();
  addField(frame, "UTC time", gps.utcTime.length() ? gps.utcTime : "--");
  addField(frame, "UTC date", gps.date.length() ? gps.date : "--");
}

void MenuSystem::fillGpsNmeaStatistics(UiFrame& frame) const {
  const GpsStatistics& stats = gps_.statistics();
  addField(frame, "Bytes", String(stats.bytesReceived));
  addField(frame, "Sentences", String(stats.sentenceCount));
  addField(frame, "Valid checksums", String(stats.validChecksumCount));
  addField(frame, "Checksum errors", String(stats.checksumErrorCount));
  addField(frame, "GGA sentences", String(stats.ggaCount));
  addField(frame, "RMC sentences", String(stats.rmcCount));
  addField(frame, "Last sentence", ageLabel(gps_.lastSentenceAgeMs()));
}

void MenuSystem::fillGpsRawNmea(UiFrame& frame) const {
  addField(frame, "Last type", gps_.lastSentenceType());
  addField(frame, "Last sentence", gps_.lastSentence().length() ? gps_.lastSentence() : "No data");
  frame.status = "Serial remains reserved for concise errors.";
}

void MenuSystem::fillStreamingInterval(UiFrame& frame) const {
  String value;
  value.reserve(16);
  for (uint8_t index = 0; index < 6; ++index) {
    if (index == 2 || index == 4) value += ':';
    if (index == editPosition_) value += editChanging_ ? '{' : '[';
    value += editDigits_[index];
    if (index == editPosition_) value += editChanging_ ? '}' : ']';
  }
  addField(frame, "Hours : Minutes : Seconds", value);
  if (editPosition_ < 6) {
    addField(frame, "Selection", "Digit " + String(editPosition_ + 1) + " of 6");
    addField(frame, "Mode", editChanging_ ? "CHANGE DIGIT 0-9" : "SELECT POSITION");
  } else {
    addField(frame, "Selection", editPosition_ == 6 ? "[SAVE CHANGES]" : "[CANCEL / BACK]");
    addField(frame, "Mode", "SELECT ACTION");
  }
  frame.subtitle = editChanging_
      ? "Rotate: digit 0-9  |  Press: finish digit"
      : "Rotate: select  |  Press: edit or activate";
  frame.status = editorMessage_.length() ? editorMessage_
      : "Valid range 00:00:01 through 99:59:59";
}

void MenuSystem::fillStreamingStatus(UiFrame& frame) const {
  const A7670Snapshot& modem = sim_.snapshot();
  addField(frame, "Streaming", streaming_.running() ? "RUNNING" : "STOPPED");
  addField(frame, "Interval", streamIntervalLabel());
  addField(frame, "GPS", settings_.simSendGps ? "Selected" : "Off");
  addField(frame, "OBD-II", settings_.simSendObd ? "Selected" : "Off");
  addField(frame, "OBD fields", String(streaming_.selectedObdFieldCount()) + " / 9");
  addField(frame, "Config revision", String(settings_.streamConfigRevision));
  addField(frame, "Heartbeat", sim_.endpointConfigured() ? "Every 30 seconds" : "Needs endpoint");
  addField(frame, "Heartbeat OK", String(modem.successfulHeartbeats));
  addField(frame, "Heartbeat failed", String(modem.failedHeartbeats));
  addField(frame, "Last heartbeat", modem.lastHeartbeatMs
      ? ageLabel(millis() - modem.lastHeartbeatMs) : "Not sent yet");
  frame.status = streaming_.statusMessage();
}

void MenuSystem::fillSimConfiguration(UiFrame& frame) const {
  const A7670Snapshot& sim = sim_.snapshot();
  addField(frame, "Modem", sim_.stateLabel());
  addField(frame, "AT response", sim.modemResponsive ? "Detected" : "No response");
  addField(frame, "SIM card", sim.simReady ? "Ready" : "Not ready");
  addField(frame, "LTE network", sim.networkRegistered ? "Registered" : "Not registered");
  addField(frame, "LTE status", sim.registrationStatus < 0 ? "Unknown"
      : String(sim.registrationStatus) + (sim.registrationStatus == 2 ? " / Searching"
      : (sim.registrationStatus == 3 ? " / Denied" : "")));
  addField(frame, "Radio / operator", String(sim.radioFunctionality) + " / " +
      (sim.operatorName.length() ? sim.operatorName : "Not selected"));
  addField(
      frame,
      "Signal CSQ",
      sim.signalQuality == 99 ? "Unknown" : String(sim.signalQuality) + " / 31");
  addField(frame, "Packet data", sim.packetAttached ? "Attached" : "Detached");
  addField(frame, "PDP context", sim.pdpActive ? sim.ipAddress : "Inactive");
  addField(frame, "APN", sim.apn.length() ? sim.apn : "SIM / modem default");
  addField(frame, "Telemetry", sim_.telemetryRunning() ? "RUNNING" : "STOPPED / press START");
  addField(
      frame,
      "Payload",
      settings_.simSendGps
          ? (settings_.simSendObd ? "GPS + OBD-II" : "GPS only")
          : (settings_.simSendObd ? "OBD-II only" : "Status only"));
  addField(
      frame,
      "Receiver",
      sim_.endpointConfigured() ? sim_.endpointLabel() : "Not configured");
  addField(
      frame,
      "Last HTTP",
      sim.lastHttpStatus ? String(sim.lastHttpStatus) : "No POST yet");
  addField(frame, "Posts", String(sim.successfulPosts) + " OK / " + sim.failedPosts + " failed");
  addField(
      frame,
      "Last error",
      sim.lastError.length() ? sim.lastError : "None");
  frame.status = sim.sending
      ? "Sending LTE request..."
      : (sim_.ready()
          ? (sim_.telemetryRunning() ? "Telemetry and heartbeat active."
                                     : "Telemetry stopped; heartbeat remains active.")
          : "Checking modem status.");
}

void MenuSystem::fillSimErrors(UiFrame& frame) const {
  const A7670Snapshot& snapshot = sim_.snapshot();
  addField(frame, "Current state", sim_.stateLabel());
  addField(
      frame,
      "Active condition",
      snapshot.lastError.length() ? snapshot.lastError : "None / recovered");
  addField(frame, "Failures recorded", String(snapshot.failureCount));

  if (snapshot.failureCount == 0) {
    addField(frame, "Last failure", "None recorded");
    addField(frame, "AT response", snapshot.modemResponsive ? "Detected" : "Not detected");
    frame.status = "No A7670SA failure has been recorded.";
    return;
  }

  addField(
      frame,
      "Failure age",
      ageLabel(millis() - snapshot.lastFailureMs));
  addField(frame, "Failure type", snapshot.lastFailureType);
  addField(frame, "Failed stage", snapshot.lastFailureStage);
  addField(frame, "Retry destination", snapshot.lastRetryTarget);
  addTextChunks(frame, "Reason", snapshot.lastFailureReason, 2);
  addTextChunks(frame, "AT command", snapshot.lastFailedCommand, 3);
  addTextChunks(
      frame,
      "Response",
      compactModemText(snapshot.lastFailureResponse),
      3);
  addField(
      frame,
      "HTTP status",
      snapshot.lastHttpStatus ? String(snapshot.lastHttpStatus) : "Not applicable");
  addField(frame, "Suggested check", simFailureHint(snapshot));
  addField(frame, "Reconnect attempts", String(snapshot.reconnectCount));
  frame.status = "Rotate to inspect each page. Press returns.";
}

void MenuSystem::fillSimEditor(UiFrame& frame, ScreenId screen) const {
  const uint8_t digitCount = simEditorDigitCount(screen);
  addField(
      frame,
      screen == ScreenId::SimIpEditor
          ? "IPv4 address"
          : (screen == ScreenId::SimPortEditor ? "TCP port" : "Six-digit key"),
      simEditorValue(screen));

  if (editPosition_ < digitCount) {
    addField(
        frame,
        "Selection",
        String("Digit ") + (editPosition_ + 1) + " of " + digitCount);
    addField(frame, "Mode", editChanging_ ? "CHANGE DIGIT" : "SELECT POSITION");
  } else {
    addField(
        frame,
        "Selection",
        editPosition_ == digitCount ? "[SAVE CHANGES]" : "[CANCEL / BACK]");
    addField(frame, "Mode", "SELECT ACTION");
  }

  frame.subtitle = editChanging_
      ? "Rotate: digit 0-9  |  Press: finish digit"
      : "Rotate: select  |  Press: edit or activate";
  frame.status = editorMessage_.length()
      ? editorMessage_
      : "Back cancels without saving.";
}

bool MenuSystem::handleSimEditorInput(InputEvent event) {
  const ScreenId screen = currentScreen();
  if (screen != ScreenId::SimIpEditor &&
      screen != ScreenId::SimPortEditor &&
      screen != ScreenId::SimKeyEditor) {
    return false;
  }

  if (event == InputEvent::Back) {
    editChanging_ = false;
    editorMessage_ = "";
    goBack();
    return true;
  }

  if (event == InputEvent::RotateLeft || event == InputEvent::RotateRight) {
    const int8_t direction = event == InputEvent::RotateRight ? 1 : -1;
    const uint8_t digitCount = simEditorDigitCount(screen);
    if (editChanging_ && editPosition_ < digitCount) {
      int8_t value = static_cast<int8_t>(editDigits_[editPosition_]) + direction;
      if (value < 0) value = 9;
      if (value > 9) value = 0;
      editDigits_[editPosition_] = static_cast<uint8_t>(value);
      editorMessage_ = "";
    } else {
      const uint8_t positionCount = digitCount + 2;
      int16_t next = static_cast<int16_t>(editPosition_) + direction;
      if (next < 0) next = positionCount - 1;
      if (next >= positionCount) next = 0;
      editPosition_ = static_cast<uint8_t>(next);
    }
    return true;
  }

  if (event == InputEvent::Press) {
    const uint8_t digitCount = simEditorDigitCount(screen);
    if (editPosition_ < digitCount) {
      editChanging_ = !editChanging_;
    } else if (editPosition_ == digitCount) {
      if (commitSimEditor(screen)) {
        editChanging_ = false;
        editorMessage_ = "";
        goBack();
      }
    } else {
      editChanging_ = false;
      editorMessage_ = "";
      goBack();
    }
    return true;
  }
  return true;
}

void MenuSystem::beginSimEditor(ScreenId screen) {
  streaming_.stop();
  editPosition_ = 0;
  editChanging_ = false;
  editorMessage_ = "";
  if (screen == ScreenId::SimIpEditor) {
    for (uint8_t octet = 0; octet < 4; ++octet) {
      const uint8_t value = settings_.simServerIp[octet];
      editDigits_[octet * 3] = value / 100;
      editDigits_[octet * 3 + 1] = (value / 10) % 10;
      editDigits_[octet * 3 + 2] = value % 10;
    }
  } else {
    const uint32_t value = screen == ScreenId::SimPortEditor
        ? settings_.simServerPort
        : settings_.simAccessKey;
    const uint8_t length = screen == ScreenId::SimPortEditor ? 5 : 6;
    uint32_t divisor = 1;
    for (uint8_t index = 1; index < length; ++index) divisor *= 10;
    for (uint8_t index = 0; index < length; ++index) {
      editDigits_[index] = (value / divisor) % 10;
      divisor /= 10;
    }
  }
  pushScreen(screen);
}

bool MenuSystem::handleStreamingIntervalInput(InputEvent event) {
  if (currentScreen() != ScreenId::StreamingInterval) return false;

  if (event == InputEvent::Back) {
    editChanging_ = false;
    editorMessage_ = "";
    goBack();
    return true;
  }

  if (event == InputEvent::RotateLeft || event == InputEvent::RotateRight) {
    const int8_t direction = event == InputEvent::RotateRight ? 1 : -1;
    if (editChanging_ && editPosition_ < 6) {
      int8_t value = static_cast<int8_t>(editDigits_[editPosition_]) + direction;
      if (value < 0) value = 9;
      if (value > 9) value = 0;
      editDigits_[editPosition_] = static_cast<uint8_t>(value);
      editorMessage_ = "";
    } else {
      int8_t next = static_cast<int8_t>(editPosition_) + direction;
      if (next < 0) next = 7;
      if (next > 7) next = 0;
      editPosition_ = static_cast<uint8_t>(next);
    }
    return true;
  }

  if (event == InputEvent::Press) {
    if (editPosition_ < 6) {
      editChanging_ = !editChanging_;
    } else if (editPosition_ == 6) {
      const uint32_t hours = editDigits_[0] * 10UL + editDigits_[1];
      const uint32_t minutes = editDigits_[2] * 10UL + editDigits_[3];
      const uint32_t seconds = editDigits_[4] * 10UL + editDigits_[5];
      const uint32_t totalSeconds = hours * 3600UL + minutes * 60UL + seconds;
      if (minutes > 59 || seconds > 59 || totalSeconds == 0) {
        editorMessage_ = "Minutes/seconds: 00-59. Minimum: 1 second.";
        editPosition_ = minutes > 59 ? 2 : (seconds > 59 ? 4 : 0);
        editChanging_ = false;
      } else {
        streaming_.setIntervalMs(totalSeconds * 1000UL);
        editChanging_ = false;
        editorMessage_ = "";
        goBack();
      }
    } else {
      editChanging_ = false;
      editorMessage_ = "";
      goBack();
    }
    return true;
  }
  return true;
}

void MenuSystem::beginStreamingIntervalEditor() {
  uint32_t totalSeconds = settings_.simSendIntervalMs / 1000UL;
  const uint8_t hours = static_cast<uint8_t>(totalSeconds / 3600UL);
  const uint8_t minutes = static_cast<uint8_t>((totalSeconds / 60UL) % 60UL);
  const uint8_t seconds = static_cast<uint8_t>(totalSeconds % 60UL);
  editDigits_[0] = hours / 10;
  editDigits_[1] = hours % 10;
  editDigits_[2] = minutes / 10;
  editDigits_[3] = minutes % 10;
  editDigits_[4] = seconds / 10;
  editDigits_[5] = seconds % 10;
  editPosition_ = 0;
  editChanging_ = false;
  editorMessage_ = "";
  pushScreen(ScreenId::StreamingInterval);
}

bool MenuSystem::commitSimEditor(ScreenId screen) {
  if (screen == ScreenId::SimIpEditor) {
    uint8_t address[4] = {};
    for (uint8_t octet = 0; octet < 4; ++octet) {
      const uint16_t value =
          editDigits_[octet * 3] * 100 +
          editDigits_[octet * 3 + 1] * 10 +
          editDigits_[octet * 3 + 2];
      if (value > 255) {
        editorMessage_ = String("Octet ") + (octet + 1) + " must be 000-255.";
        editPosition_ = octet * 3;
        editChanging_ = false;
        return false;
      }
      address[octet] = static_cast<uint8_t>(value);
    }
    memcpy(settings_.simServerIp, address, sizeof(address));
  } else {
    const uint8_t length = screen == ScreenId::SimPortEditor ? 5 : 6;
    uint32_t value = 0;
    for (uint8_t index = 0; index < length; ++index) {
      value = value * 10 + editDigits_[index];
    }
    if (screen == ScreenId::SimPortEditor) {
      if (value == 0 || value > 65535) {
        editorMessage_ = "Port must be 00001-65535.";
        editPosition_ = 0;
        editChanging_ = false;
        return false;
      }
      settings_.simServerPort = static_cast<uint16_t>(value);
    } else {
      settings_.simAccessKey = value;
    }
  }
  sim_.setEndpoint(
      settings_.simServerIp,
      settings_.simServerPort,
      settings_.simAccessKey);
  settingsStore_.saveSim(settings_);
  return true;
}

uint8_t MenuSystem::simEditorDigitCount(ScreenId screen) const {
  if (screen == ScreenId::SimIpEditor) return 12;
  return screen == ScreenId::SimPortEditor ? 5 : 6;
}

String MenuSystem::simEditorValue(ScreenId screen) const {
  const uint8_t digitCount = simEditorDigitCount(screen);
  String value;
  value.reserve(20);
  for (uint8_t index = 0; index < digitCount; ++index) {
    if (screen == ScreenId::SimIpEditor && index > 0 && index % 3 == 0) {
      value += '.';
    }
    if (index == editPosition_) value += editChanging_ ? '{' : '[';
    value += editDigits_[index];
    if (index == editPosition_) value += editChanging_ ? '}' : ']';
  }
  return value;
}

String MenuSystem::compactModemText(const String& value) const {
  String output = value;
  output.replace("\r", " ");
  output.replace("\n", " ");
  output.trim();
  while (output.indexOf("  ") >= 0) output.replace("  ", " ");
  return output.length() ? output : "No response captured";
}

String MenuSystem::simFailureHint(const A7670Snapshot& snapshot) const {
  String context = snapshot.lastFailureReason + " " +
      snapshot.lastFailureStage + " " + snapshot.lastFailureType;
  context.toLowerCase();
  if (context.indexOf("no response") >= 0 ||
      context.indexOf("synchron") >= 0) {
    return "Power/GND/TX/RX/baud";
  }
  if (context.indexOf("sim") >= 0) return "SIM seated and unlocked";
  if (context.indexOf("register") >= 0 ||
      context.indexOf("signal") >= 0) {
    return "Antenna/LTE coverage";
  }
  if (context.indexOf("packet") >= 0 ||
      context.indexOf("pdp") >= 0 ||
      context.indexOf("apn") >= 0) {
    return "APN and SIM data plan";
  }
  if (context.indexOf("http") >= 0 ||
      context.indexOf("receiver") >= 0 ||
      context.indexOf("settings") >= 0) {
    return "IP/port/key/receiver";
  }
  return "Review AT cmd/response";
}

void MenuSystem::addTextChunks(
    UiFrame& frame,
    const String& label,
    const String& value,
    uint8_t maxChunks) const {
  const String text = value.length() ? value : "None";
  constexpr uint8_t CHUNK_LENGTH = 23;
  uint8_t chunk = 0;
  for (uint16_t start = 0;
       start < text.length() && chunk < maxChunks;
       start += CHUNK_LENGTH, ++chunk) {
    addField(
        frame,
        maxChunks > 1 ? label + " " + String(chunk + 1) : label,
        text.substring(start, start + CHUNK_LENGTH));
  }
}

String MenuSystem::ipLabel(const uint8_t ip[4]) const {
  return String(ip[0]) + "." + ip[1] + "." + ip[2] + "." + ip[3];
}

String MenuSystem::streamIntervalLabel() const {
  const uint32_t totalSeconds = settings_.simSendIntervalMs / 1000UL;
  const uint8_t hours = static_cast<uint8_t>(totalSeconds / 3600UL);
  const uint8_t minutes = static_cast<uint8_t>((totalSeconds / 60UL) % 60UL);
  const uint8_t seconds = static_cast<uint8_t>(totalSeconds % 60UL);
  char value[9];
  snprintf(value, sizeof(value), "%02u:%02u:%02u", hours, minutes, seconds);
  return String(value);
}

uint8_t MenuSystem::selectedFieldCount(uint16_t mask) const {
  uint8_t count = 0;
  while (mask) {
    count += mask & 1U;
    mask >>= 1;
  }
  return count;
}

void MenuSystem::fillAbout(UiFrame& frame) const {
  addField(frame, "Project", "Roderic Systems RoadLink");
  addField(frame, "Controller", "ESP32 + MCP2515 8 MHz");
  addField(frame, "UI architecture", "UiModel -> TFT only");
  addField(frame, "TFT", "ILI9341 320x240 landscape");
  addField(frame, "Remote control", "Semantic LTE config heartbeat");
  addField(frame, "Navigation", "Rotary tree with explicit Back");
  addField(frame, "CAN diagnostics", "Passive monitor + OBD-II ISO-TP");
  addField(frame, "Cellular", "A7670SA LTE HTTP telemetry");
}

bool MenuSystem::ensureObdTransmitMode() {
  if (!can_.initialized()) return false;

  if (settings_.canMode != CanOperatingMode::Normal || !can_.canTransmit()) {
    settings_.canMode = CanOperatingMode::Normal;
    if (!can_.reinitialize(settings_.canBitrate, settings_.canMode)) {
      return false;
    }
  }
  return true;
}

void MenuSystem::retryCan() {
  can_.reinitialize(settings_.canBitrate, settings_.canMode);
}

void MenuSystem::cycleCanBitrate() {
  switch (settings_.canBitrate) {
    case CanBitrate::K125:  settings_.canBitrate = CanBitrate::K250; break;
    case CanBitrate::K250:  settings_.canBitrate = CanBitrate::K500; break;
    case CanBitrate::K500:  settings_.canBitrate = CanBitrate::K1000; break;
    case CanBitrate::K1000: settings_.canBitrate = CanBitrate::K125; break;
  }
  can_.reinitialize(settings_.canBitrate, settings_.canMode);
}

void MenuSystem::toggleCanMode() {
  settings_.canMode = settings_.canMode == CanOperatingMode::ListenOnly
      ? CanOperatingMode::Normal
      : CanOperatingMode::ListenOnly;
  can_.reinitialize(settings_.canBitrate, settings_.canMode);

  if (settings_.canMode == CanOperatingMode::ListenOnly) {
    obd_.cancelOperation();
  }
}

void MenuSystem::cycleUiRefresh() {
  if (settings_.uiRefreshMs <= 100) settings_.uiRefreshMs = 150;
  else if (settings_.uiRefreshMs <= 150) settings_.uiRefreshMs = 250;
  else if (settings_.uiRefreshMs <= 250) settings_.uiRefreshMs = 500;
  else settings_.uiRefreshMs = 100;
}

void MenuSystem::cycleObdPoll() {
  if (settings_.obdPollMs <= 80) settings_.obdPollMs = 120;
  else if (settings_.obdPollMs <= 120) settings_.obdPollMs = 200;
  else if (settings_.obdPollMs <= 200) settings_.obdPollMs = 500;
  else settings_.obdPollMs = 80;
  obd_.setPollInterval(settings_.obdPollMs);
}

String MenuSystem::formatHexId(uint32_t id, bool extended) const {
  char buffer[20];
  if (extended) snprintf(buffer, sizeof(buffer), "EXT 0x%08lX", static_cast<unsigned long>(id));
  else snprintf(buffer, sizeof(buffer), "0x%03lX", static_cast<unsigned long>(id));
  return String(buffer);
}

String MenuSystem::formatData(const uint8_t* data, uint8_t dlc) const {
  String output;
  for (uint8_t index = 0; index < dlc; ++index) {
    if (index) output += ' ';
    if (data[index] < 0x10) output += '0';
    output += String(data[index], HEX);
  }
  output.toUpperCase();
  return output.length() ? output : "--";
}

String MenuSystem::ageLabel(uint32_t ageMs) const {
  if (ageMs == UINT32_MAX) return "No data";
  if (ageMs < 1000) return String(ageMs) + " ms";
  return String(ageMs / 1000.0f, 1) + " s";
}

String MenuSystem::floatValue(
    bool valid,
    float value,
    uint8_t decimals,
    const char* unit) const {
  if (!valid) return "--";
  return String(value, static_cast<unsigned int>(decimals)) + " " + unit;
}

String MenuSystem::supportedMaskLabel(const ObdEcuInfo* ecu, uint8_t index) const {
  if (!ecu || index >= 4 || !ecu->supportedMaskValid[index]) return "Not scanned";
  char buffer[11];
  snprintf(buffer, sizeof(buffer), "0x%08lX", static_cast<unsigned long>(ecu->supportedMasks[index]));
  return String(buffer);
}
