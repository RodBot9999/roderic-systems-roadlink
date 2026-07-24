#pragma once

#include <Arduino.h>
#include "AppState.h"
#include "EncoderInput.h"
#include "UiModel.h"
#include "GpsService.h"
#include "CanService.h"
#include "ObdService.h"
#include "Sim800Service.h"
#include "SettingsStore.h"
#include "StartupDiagnostics.h"
#include "WebUiService.h"

enum class ScreenId : uint8_t {
  StartupErrors,
  StartupErrorDetail,
  MainMenu,

  CanMenu,
  CanMonitorMenu,
  CanStatus,
  CanLiveFrame,
  CanIdBrowser,
  CanIdDetail,
  CanStatistics,

  ObdMenu,
  ObdStatus,
  ObdDiscovery,
  ObdLiveData,
  ObdSupportedPids,
  ObdDtcs,
  ObdClearConfirm,
  ObdVin,

  GpsMenu,
  GpsOverview,
  GpsPosition,
  GpsSignal,
  GpsMotion,
  GpsTime,
  GpsNmeaStatistics,
  GpsRawNmea,

  SettingsMenu,
  SimConfiguration,
  SimStatus,
  SimDataSelection,
  SimIpEditor,
  SimPortEditor,
  SimKeyEditor,
  About
};

class MenuSystem {
public:
  MenuSystem(
      UiModel& ui,
      CanService& can,
      ObdService& obd,
      GpsService& gps,
      Sim800Service& sim,
      AppSettings& settings,
      SettingsStore& settingsStore,
      StartupDiagnostics& diagnostics,
      WebUiService& webUi);

  void begin();
  void handleInput(InputEvent event);
  void update();

private:
  static constexpr uint8_t NAVIGATION_DEPTH = 10;

  struct NavEntry {
    ScreenId screen = ScreenId::MainMenu;
    uint8_t selection = 0;
  };

  ScreenId currentScreen() const;
  uint8_t& currentSelection();
  const uint8_t& currentSelection() const;

  void pushScreen(ScreenId screen);
  void goBack();
  void onScreenChanged(ScreenId previous, ScreenId current);
  void rotate(int8_t direction);
  void press();

  uint8_t itemCount(ScreenId screen) const;
  String itemLabel(ScreenId screen, uint8_t index) const;
  String itemValue(ScreenId screen, uint8_t index) const;
  bool itemEnabled(ScreenId screen, uint8_t index) const;
  bool itemDestructive(ScreenId screen, uint8_t index) const;
  void activateItem(ScreenId screen, uint8_t index);

  void render(bool force = false);
  UiFrame buildFrame(ScreenId screen) const;
  String titleFor(ScreenId screen) const;
  String breadcrumbFor(ScreenId screen) const;
  UiLayout layoutFor(ScreenId screen) const;

  void addItem(
      UiFrame& frame,
      const String& label,
      const String& value = "",
      bool enabled = true,
      bool destructive = false) const;
  void addField(UiFrame& frame, const String& label, const String& value) const;

  void fillStartupErrorDetail(UiFrame& frame) const;
  void fillCanStatus(UiFrame& frame) const;
  void fillCanLiveFrame(UiFrame& frame) const;
  void fillCanIdDetail(UiFrame& frame) const;
  void fillCanStatistics(UiFrame& frame) const;
  void fillObdStatus(UiFrame& frame) const;
  void fillObdDiscovery(UiFrame& frame) const;
  void fillObdLiveData(UiFrame& frame) const;
  void fillObdSupportedPids(UiFrame& frame) const;
  void fillObdDtcs(UiFrame& frame) const;
  void fillObdVin(UiFrame& frame) const;
  void fillGpsOverview(UiFrame& frame) const;
  void fillGpsPosition(UiFrame& frame) const;
  void fillGpsSignal(UiFrame& frame) const;
  void fillGpsMotion(UiFrame& frame) const;
  void fillGpsTime(UiFrame& frame) const;
  void fillGpsNmeaStatistics(UiFrame& frame) const;
  void fillGpsRawNmea(UiFrame& frame) const;
  void fillSimConfiguration(UiFrame& frame) const;
  void fillSimEditor(UiFrame& frame, ScreenId screen) const;
  void fillAbout(UiFrame& frame) const;

  bool ensureObdTransmitMode();
  void retryCan();
  void cycleCanBitrate();
  void toggleCanMode();
  void cycleUiRefresh();
  void cycleObdPoll();
  void cycleSimInterval();
  bool handleSimEditorInput(InputEvent event);
  void beginSimEditor(ScreenId screen);
  void commitSimEditor(ScreenId screen);
  String ipLabel(const uint8_t ip[4]) const;

  String formatHexId(uint32_t id, bool extended = false) const;
  String formatData(const uint8_t* data, uint8_t dlc) const;
  String ageLabel(uint32_t ageMs) const;
  String floatValue(bool valid, float value, uint8_t decimals, const char* unit) const;
  String supportedMaskLabel(const ObdEcuInfo* ecu, uint8_t index) const;

  UiModel& ui_;
  CanService& can_;
  ObdService& obd_;
  GpsService& gps_;
  Sim800Service& sim_;
  AppSettings& settings_;
  SettingsStore& settingsStore_;
  StartupDiagnostics& diagnostics_;
  WebUiService& webUi_;

  NavEntry navigation_[NAVIGATION_DEPTH] = {};
  uint8_t navigationDepth_ = 1;
  uint8_t selectedErrorIndex_ = 0;
  uint8_t selectedCanIdIndex_ = 0;
  uint32_t lastRenderMs_ = 0;
  uint8_t editIp_[4] = {};
  uint8_t editDigits_[6] = {};
  uint8_t editPosition_ = 0;
};
