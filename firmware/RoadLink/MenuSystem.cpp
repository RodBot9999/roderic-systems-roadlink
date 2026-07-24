#include "MenuSystem.h"
#include "AppConfig.h"

MenuSystem::MenuSystem(
    UiModel& ui,
    CanService& can,
    ObdService& obd,
    GpsService& gps,
    Sim800Service& sim,
    AppSettings& settings,
    SettingsStore& settingsStore,
    StartupDiagnostics& diagnostics,
    WebUiService& webUi)
  : ui_(ui),
    can_(can),
    obd_(obd),
    gps_(gps),
    sim_(sim),
    settings_(settings),
    settingsStore_(settingsStore),
    diagnostics_(diagnostics),
    webUi_(webUi) {
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

  if (handleSimEditorInput(event)) {
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
    navigationDepth_++;
  } else {
    navigation_[NAVIGATION_DEPTH - 1].screen = screen;
    navigation_[NAVIGATION_DEPTH - 1].selection = 0;
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
  if (previous == ScreenId::GpsRawNmea && current != ScreenId::GpsRawNmea) {
    gps_.setRawSerialEnabled(false);
  }

  if (previous == ScreenId::ObdLiveData && current != ScreenId::ObdLiveData) {
    obd_.setLivePolling(false);
  }

  if (current == ScreenId::ObdLiveData) {
    if (ensureObdTransmitMode()) {
      obd_.setLivePolling(true);
    }
  }
}

void MenuSystem::rotate(int8_t direction) {
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
    case ScreenId::MainMenu:           return 4;
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
    case ScreenId::GpsRawNmea:         return 2;
    case ScreenId::SettingsMenu:       return 9;
    case ScreenId::SimConfiguration:   return 11;
    case ScreenId::SimStatus:          return 1;
    case ScreenId::SimDataSelection:   return 3;
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
      "CAN Tools", "GPS Tools", "SIM / Cellular", "Settings"
    };
    return ITEMS[index];
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
      "CAN Bitrate", "CAN Operating Mode", "Serial CAN Stream", "Serial UI Mirror",
      "Web UI", "UI Refresh", "OBD Poll Interval", "About", "< Back"
    };
    return ITEMS[index];
  }

  if (screen == ScreenId::CanIdBrowser) {
    if (index >= can_.statistics().uniqueIdCount) return "< Back";
    const CanIdEntry* entry = can_.idEntry(index);
    return entry ? formatHexId(entry->id, entry->extended) : "Unavailable ID";
  }

  switch (screen) {
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
      return "< Back";

    case ScreenId::CanStatus:
      return index == 0 ? "Retry CAN Controller" : "< Back";
    case ScreenId::CanStatistics:
      return index == 0 ? "Clear Statistics" : "< Back";
    case ScreenId::ObdDiscovery:
      return index == 0 ? "Start ECU Discovery" : "< Back";
    case ScreenId::ObdLiveData:
      if (index == 0) return obd_.livePollingEnabled() ? "Stop Live Polling" : "Start Live Polling";
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
      return index == 0 ? "Toggle Raw Serial" : "< Back";
    case ScreenId::SimConfiguration:
      switch (index) {
        case 0: return "Status / diagnostics";
        case 1: return sim_.enabled() ? "Disable modem" : "Enable modem";
        case 2: return sim_.autoSend() ? "Disable auto send" : "Enable auto send";
        case 3: return "Data to send";
        case 4: return "Receiver IP";
        case 5: return "Receiver port";
        case 6: return "Access key";
        case 7: return "Send telemetry now";
        case 8: return "Send interval";
        case 9: return "Reset / reconnect";
        default: return "< Back";
      }
    case ScreenId::SimDataSelection:
      if (index == 0) {
        return settings_.simSendGps ? "[X] GPS telemetry" : "[ ] GPS telemetry";
      }
      if (index == 1) {
        return settings_.simSendObd ? "[X] OBD-II telemetry" : "[ ] OBD-II telemetry";
      }
      return "< Back";
    default:
      return "?";
  }
}

String MenuSystem::itemValue(ScreenId screen, uint8_t index) const {
  if (screen == ScreenId::SettingsMenu) {
    switch (index) {
      case 0: return canBitrateLabel(settings_.canBitrate);
      case 1: return canModeLabel(settings_.canMode);
      case 2: return settings_.serialCanStreaming ? "ON" : "OFF";
      case 3: return settings_.serialUiMirror ? "ON" : "OFF";
      case 4: return settings_.webUiEnabled ? "ON" : "OFF";
      case 5: return String(settings_.uiRefreshMs) + " ms";
      case 6: return String(settings_.obdPollMs) + " ms";
      default: return "";
    }
  }

  if (screen == ScreenId::SimConfiguration) {
    switch (index) {
      case 0: return sim_.stateLabel();
      case 1: return sim_.enabled() ? "ON" : "OFF";
      case 2: return sim_.autoSend() ? "ON" : "OFF";
      case 3:
        if (settings_.simSendGps && settings_.simSendObd) return "GPS + OBD";
        if (settings_.simSendGps) return "GPS only";
        if (settings_.simSendObd) return "OBD only";
        return "STATUS ONLY";
      case 4: return ipLabel(settings_.simServerIp);
      case 5: return settings_.simServerPort
          ? String(settings_.simServerPort) : "NOT SET";
      case 6: {
        char key[7];
        snprintf(key, sizeof(key), "%06lu",
            static_cast<unsigned long>(settings_.simAccessKey));
        return key;
      }
      case 7: return sim_.ready() ? "READY" : "QUEUED";
      case 8: return String(sim_.sendInterval() / 1000) + " s";
      default: return "";
    }
  }

  if (screen == ScreenId::CanIdBrowser && index < can_.statistics().uniqueIdCount) {
    const CanIdEntry* entry = can_.idEntry(index);
    return entry ? String(entry->count) + " frames" : "";
  }

  if (screen == ScreenId::GpsRawNmea && index == 0) {
    return gps_.rawSerialEnabled() ? "ON" : "OFF";
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
  if (screen == ScreenId::SimConfiguration &&
      (index == 2 || index == 7 || index == 8 || index == 9)) {
    return sim_.enabled();
  }
  return true;
}

bool MenuSystem::itemDestructive(ScreenId screen, uint8_t index) const {
  return (screen == ScreenId::ObdClearConfirm && index == 1) ||
         (screen == ScreenId::CanMenu && index == 4);
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
    if (index == 0) pushScreen(ScreenId::CanMenu);
    else if (index == 1) pushScreen(ScreenId::GpsMenu);
    else if (index == 2) pushScreen(ScreenId::SimConfiguration);
    else pushScreen(ScreenId::SettingsMenu);
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
      case 2:
        settings_.serialCanStreaming = !settings_.serialCanStreaming;
        can_.setSerialStreaming(settings_.serialCanStreaming);
        break;
      case 3:
        settings_.serialUiMirror = !settings_.serialUiMirror;
        ui_.setSerialMirror(settings_.serialUiMirror);
        break;
      case 4: {
        const bool requested = !settings_.webUiEnabled;
        settings_.webUiEnabled = webUi_.setEnabled(requested);
        break;
      }
      case 5: cycleUiRefresh(); break;
      case 6: cycleObdPoll(); break;
      case 7: pushScreen(ScreenId::About); break;
      case 8: goBack(); break;
    }
    return;
  }

  switch (screen) {
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
      if (index == 0) gps_.setRawSerialEnabled(!gps_.rawSerialEnabled());
      else {
        gps_.setRawSerialEnabled(false);
        goBack();
      }
      break;
    case ScreenId::SimConfiguration:
      switch (index) {
        case 0:
          pushScreen(ScreenId::SimStatus);
          break;
        case 1:
          settings_.simEnabled = !settings_.simEnabled;
          sim_.setEnabled(settings_.simEnabled);
          settingsStore_.saveSim(settings_);
          break;
        case 2:
          settings_.simAutoSend = !settings_.simAutoSend;
          sim_.setAutoSend(settings_.simAutoSend);
          settingsStore_.saveSim(settings_);
          break;
        case 3:
          pushScreen(ScreenId::SimDataSelection);
          break;
        case 4:
          beginSimEditor(ScreenId::SimIpEditor);
          break;
        case 5:
          beginSimEditor(ScreenId::SimPortEditor);
          break;
        case 6:
          beginSimEditor(ScreenId::SimKeyEditor);
          break;
        case 7:
          sim_.requestSendNow();
          break;
        case 8:
          cycleSimInterval();
          settingsStore_.saveSim(settings_);
          break;
        case 9:
          sim_.requestReset();
          break;
        case 10:
          goBack();
          break;
      }
      break;
    case ScreenId::SimDataSelection:
      if (index == 0) {
        settings_.simSendGps = !settings_.simSendGps;
      } else if (index == 1) {
        settings_.simSendObd = !settings_.simSendObd;
      } else {
        goBack();
        break;
      }
      sim_.setPayloadSelection(settings_.simSendGps, settings_.simSendObd);
      settingsStore_.saveSim(settings_);
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

  const uint8_t count = itemCount(screen);
  for (uint8_t index = 0; index < count; ++index) {
    addItem(
        frame,
        itemLabel(screen, index),
        itemValue(screen, index),
        itemEnabled(screen, index),
        itemDestructive(screen, index));
  }

  switch (screen) {
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
  }

  return frame;
}

String MenuSystem::titleFor(ScreenId screen) const {
  switch (screen) {
    case ScreenId::StartupErrors:      return "Startup Diagnostics";
    case ScreenId::StartupErrorDetail: return "Error Details";
    case ScreenId::MainMenu:           return "Scan-Track-Log";
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
    case ScreenId::SimConfiguration:   return "SIM Configuration";
    case ScreenId::SimStatus:          return "SIM Status";
    case ScreenId::SimDataSelection:   return "Telemetry Selection";
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
      return UiLayout::Alert;
    case ScreenId::MainMenu:
    case ScreenId::CanMenu:
    case ScreenId::CanMonitorMenu:
    case ScreenId::CanIdBrowser:
    case ScreenId::ObdMenu:
    case ScreenId::GpsMenu:
    case ScreenId::SettingsMenu:
    case ScreenId::SimConfiguration:
    case ScreenId::SimDataSelection:
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
    bool destructive) const {
  if (frame.itemCount >= UI_MAX_ITEMS) return;
  UiItem& item = frame.items[frame.itemCount++];
  item.label = label;
  item.value = value;
  item.enabled = enabled;
  item.destructive = destructive;
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
  frame.status = "Raw NMEA can also be streamed to Serial Monitor.";
}

void MenuSystem::fillSimConfiguration(UiFrame& frame) const {
  const Sim800Snapshot& sim = sim_.snapshot();
  addField(frame, "Modem", sim_.stateLabel());
  addField(frame, "AT response", sim.modemResponsive ? "Detected" : "No response");
  addField(frame, "SIM card", sim.simReady ? "Ready" : "Not ready");
  addField(frame, "GSM network", sim.networkRegistered ? "Registered" : "Not registered");
  addField(
      frame,
      "Signal CSQ",
      sim.signalQuality == 99 ? "Unknown" : String(sim.signalQuality) + " / 31");
  addField(frame, "GPRS", sim.gprsAttached ? "Attached" : "Detached");
  addField(frame, "Bearer", sim.bearerOpen ? sim.ipAddress : "Closed");
  addField(frame, "APN", AppConfig::SIM_APN);
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
      ? "Sending GPS + OBD-II telemetry..."
      : (sim_.ready()
          ? "Cellular telemetry is ready."
          : "Enter the IP, port, and key shown by the desktop app.");
}

void MenuSystem::fillSimEditor(UiFrame& frame, ScreenId screen) const {
  if (screen == ScreenId::SimIpEditor) {
    String value;
    for (uint8_t index = 0; index < 4; ++index) {
      if (index) value += '.';
      if (index == editPosition_) value += '[';
      value += editIp_[index];
      if (index == editPosition_) value += ']';
    }
    addField(frame, "IPv4 address", value);
    frame.subtitle = "Rotate: change octet  |  Press: next";
  } else {
    const uint8_t length = screen == ScreenId::SimPortEditor ? 5 : 6;
    String value;
    for (uint8_t index = 0; index < length; ++index) {
      if (index == editPosition_) value += '[';
      value += editDigits_[index];
      if (index == editPosition_) value += ']';
    }
    addField(
        frame,
        screen == ScreenId::SimPortEditor ? "TCP port" : "Six-digit key",
        value);
    frame.subtitle = "Rotate: change digit  |  Press: next";
  }
  frame.status = "Back cancels. Press on the final value to save.";
}

bool MenuSystem::handleSimEditorInput(InputEvent event) {
  const ScreenId screen = currentScreen();
  if (screen != ScreenId::SimIpEditor &&
      screen != ScreenId::SimPortEditor &&
      screen != ScreenId::SimKeyEditor) {
    return false;
  }

  if (event == InputEvent::Back) {
    goBack();
    return true;
  }

  if (event == InputEvent::RotateLeft || event == InputEvent::RotateRight) {
    const int8_t direction = event == InputEvent::RotateRight ? 1 : -1;
    if (screen == ScreenId::SimIpEditor) {
      int16_t value = static_cast<int16_t>(editIp_[editPosition_]) + direction;
      if (value < 0) value = 255;
      if (value > 255) value = 0;
      editIp_[editPosition_] = static_cast<uint8_t>(value);
    } else {
      int8_t value = static_cast<int8_t>(editDigits_[editPosition_]) + direction;
      if (value < 0) value = 9;
      if (value > 9) value = 0;
      editDigits_[editPosition_] = static_cast<uint8_t>(value);
    }
    return true;
  }

  if (event == InputEvent::Press) {
    const uint8_t length = screen == ScreenId::SimIpEditor
        ? 4
        : (screen == ScreenId::SimPortEditor ? 5 : 6);
    if (++editPosition_ >= length) {
      commitSimEditor(screen);
      goBack();
    }
    return true;
  }
  return true;
}

void MenuSystem::beginSimEditor(ScreenId screen) {
  editPosition_ = 0;
  if (screen == ScreenId::SimIpEditor) {
    memcpy(editIp_, settings_.simServerIp, sizeof(editIp_));
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

void MenuSystem::commitSimEditor(ScreenId screen) {
  if (screen == ScreenId::SimIpEditor) {
    memcpy(settings_.simServerIp, editIp_, sizeof(editIp_));
  } else {
    const uint8_t length = screen == ScreenId::SimPortEditor ? 5 : 6;
    uint32_t value = 0;
    for (uint8_t index = 0; index < length; ++index) {
      value = value * 10 + editDigits_[index];
    }
    if (screen == ScreenId::SimPortEditor) {
      if (value == 0) value = 1;
      if (value > 65535) value = 65535;
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
}

String MenuSystem::ipLabel(const uint8_t ip[4]) const {
  return String(ip[0]) + "." + ip[1] + "." + ip[2] + "." + ip[3];
}

void MenuSystem::fillAbout(UiFrame& frame) const {
  addField(frame, "Project", "Roderic Systems RoadLink");
  addField(frame, "Controller", "ESP32 + MCP2515 8 MHz");
  addField(frame, "UI architecture", "UiModel -> TFT / WebSocket / Serial");
  addField(frame, "TFT", "ILI9341 320x240 landscape");
  addField(frame, "Phone transport", "Local HTTP + live WebSocket");
  addField(frame, "Navigation", "Rotary tree with explicit Back");
  addField(frame, "CAN diagnostics", "Passive monitor + OBD-II ISO-TP");
  addField(frame, "Cellular", "SIM800L HTTP telemetry");
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

void MenuSystem::cycleSimInterval() {
  const uint32_t current = settings_.simSendIntervalMs;
  if (current <= 5000) settings_.simSendIntervalMs = 10000;
  else if (current <= 10000) settings_.simSendIntervalMs = 30000;
  else if (current <= 30000) settings_.simSendIntervalMs = 60000;
  else settings_.simSendIntervalMs = 5000;
  sim_.setSendInterval(settings_.simSendIntervalMs);
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
