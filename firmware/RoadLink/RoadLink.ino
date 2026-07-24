#include <Arduino.h>

#include "AppConfig.h"
#include "AppState.h"
#include "StartupDiagnostics.h"
#include "EncoderInput.h"
#include "GpsService.h"
#include "CanService.h"
#include "ObdService.h"
#include "Sim800Service.h"
#include "SettingsStore.h"
#include "UiModel.h"
#include "WebUiService.h"
#include "TftRenderer.h"
#include "MenuSystem.h"

AppSettings settings;
SettingsStore settingsStore;

EncoderInput encoder(
    Pins::ENCODER_CLK,
    Pins::ENCODER_DT,
    Pins::ENCODER_SW);

GpsService gps(2);
CanService can(Pins::CAN_CS, Pins::CAN_INT);
ObdService obd(can);
Sim800Service sim(gps, obd, 1);
UiModel ui;
StartupDiagnostics diagnostics;
TftRenderer tft(
    ui,
    Pins::TFT_CS,
    Pins::TFT_DC,
    Pins::TFT_RST);

WebUiService webUi(
    AppConfig::WEB_HTTP_PORT,
    AppConfig::WEB_SOCKET_PORT,
    AppConfig::WEB_UI_SSID,
    AppConfig::WEB_UI_PASSWORD,
    ui);

MenuSystem menu(
    ui,
    can,
    obd,
    gps,
    sim,
    settings,
    settingsStore,
    diagnostics,
    webUi);

void waitForBootPress(uint32_t timeoutMs = 0) {
  const uint32_t startedMs = millis();

  while (timeoutMs == 0 || millis() - startedMs < timeoutMs) {
    if (encoder.poll() == InputEvent::Press) {
      return;
    }
    delay(1);
  }
}

bool probeOptionalModules() {
  const uint32_t startedMs = millis();
  bool skipRequested = false;

  while (millis() - startedMs < AppConfig::STARTUP_MODULE_PROBE_MS) {
    gps.update();
    sim.update();
    can.update();
    obd.update();
    webUi.update();

    if (encoder.poll() == InputEvent::Press) {
      skipRequested = true;
      break;
    }

    const bool gpsDetected = gps.statistics().bytesReceived > 0;
    const bool simDetected =
        !settings.simEnabled || sim.snapshot().modemResponsive;
    if (gpsDetected && simDetected) break;
    delay(1);
  }

  const bool gpsDetected = gps.statistics().bytesReceived > 0;
  if (gpsDetected) {
    tft.setSystemCheck(2, "GPS RECEIVER", "DETECTED", BootCheckState::Ok);
  } else {
    diagnostics.reportWarning(
        ModuleId::Gps,
        "No GPS serial data detected during startup",
        static_cast<int32_t>(gps.statistics().bytesReceived),
        static_cast<int32_t>(AppConfig::STARTUP_MODULE_PROBE_MS));
    tft.setSystemCheck(2, "GPS RECEIVER", "WARNING", BootCheckState::Warning);
  }

  if (!settings.simEnabled) {
    tft.setSystemCheck(5, "SIM800L MODEM", "DISABLED", BootCheckState::Off);
  } else if (sim.snapshot().modemResponsive) {
    tft.setSystemCheck(5, "SIM800L MODEM", "DETECTED", BootCheckState::Ok);
  } else {
    diagnostics.reportWarning(
        ModuleId::Sim,
        skipRequested
            ? "SIM800L startup check skipped"
            : "SIM800L did not respond to an AT command",
        static_cast<int32_t>(sim.state()),
        static_cast<int32_t>(AppConfig::STARTUP_MODULE_PROBE_MS));
    tft.setSystemCheck(5, "SIM800L MODEM", "WARNING", BootCheckState::Warning);
  }

  return skipRequested;
}

void setup() {
  Serial.begin(AppConfig::USB_BAUD);
  delay(300);

  Serial.println();
  Serial.println(F("============================================================"));
  Serial.println(F("Roderic Systems RoadLink"));
  Serial.println(F("TFT + renderer-independent UI + CAN/OBD + GPS + WebSocket"));
  Serial.println(F("============================================================"));

  settingsStore.begin();
  settingsStore.load(settings);

  // The TFT and MCP2515 share SCK/MOSI/MISO. Their CS pins are separate.
  tft.begin(
      Pins::CAN_SCK,
      Pins::CAN_MISO,
      Pins::CAN_MOSI,
      AppConfig::TFT_ROTATION,
      AppConfig::TFT_SPI_HZ);

  encoder.begin();
  tft.showSplash();
  waitForBootPress();
  tft.beginSystemCheck();
  diagnostics.clear();
  tft.setSystemCheck(0, "ROTARY INPUT", "OK", BootCheckState::Ok);

  ui.begin(settings.serialUiMirror);
  tft.setSystemCheck(1, "UI CORE", "OK", BootCheckState::Ok);

  gps.begin(
      AppConfig::GPS_BAUD,
      Pins::GPS_RX,
      Pins::GPS_TX,
      Pins::GPS_PPS);
  tft.setSystemCheck(2, "GPS RECEIVER", "WAIT", BootCheckState::Pending);

  obd.begin(settings.obdPollMs);
  tft.setSystemCheck(3, "OBD SERVICE", "READY", BootCheckState::Ok);

  // Initialize the shared SPI CAN controller before starting the optional
  // modem UART. A disconnected UART RX can otherwise float and flood the CPU
  // with receive interrupts while MCP2515 setup is using the shared SPI bus.
  Serial.println(F("[BOOT] Initializing MCP2515 CAN controller"));
  const bool canReady = can.begin(
      settings.canBitrate,
      settings.canMode);

  if (!canReady) {
    diagnostics.report(
        ModuleId::Can,
        "MCP2515 initialization failed",
        can.initializationResult(),
        can.controllerError());
    tft.setSystemCheck(4, "CAN CONTROLLER", "ERROR", BootCheckState::Error);
    Serial.println(F("[BOOT] MCP2515 initialization failed; continuing"));
  } else {
    tft.setSystemCheck(4, "CAN CONTROLLER", "OK", BootCheckState::Ok);
    Serial.println(F("[BOOT] MCP2515 ready"));
  }

  can.setSerialStreaming(settings.serialCanStreaming);

  Serial.println(F("[BOOT] Starting optional SIM800L UART"));
  sim.begin(
      AppConfig::SIM_BAUD,
      Pins::SIM_RX,
      Pins::SIM_TX,
      Pins::SIM_RST,
      settings.simEnabled,
      settings.simAutoSend,
      settings.simSendGps,
      settings.simSendObd,
      settings.simSendIntervalMs,
      settings.simServerIp,
      settings.simServerPort,
      settings.simAccessKey);
  tft.setSystemCheck(
      5,
      "SIM800L MODEM",
      settings.simEnabled ? "WAIT" : "DISABLED",
      settings.simEnabled ? BootCheckState::Pending : BootCheckState::Off);

  // The HTTP page and WebSocket are hosted locally by the ESP32 SoftAP.
  Serial.println(F("[BOOT] Starting local web service"));
  settings.webUiEnabled = webUi.begin(settings.webUiEnabled);
  tft.setSystemCheck(
      6,
      "WEB SOCKET",
      settings.webUiEnabled ? "OK" : "OFF",
      settings.webUiEnabled ? BootCheckState::Ok : BootCheckState::Warning);

  const bool startupSkipRequested = probeOptionalModules();

  tft.finishSystemCheck(
      diagnostics.hasFatalErrors()
          ? "BOOT COMPLETE / ERRORS LOGGED"
          : (diagnostics.hasWarnings()
              ? "BOOT COMPLETE / WARNINGS LOGGED"
              : "BOOT COMPLETE / ALL SYSTEMS READY"));
  if (!startupSkipRequested) {
    waitForBootPress(AppConfig::TFT_BOOT_HOLD_MS);
  }

  // Publish the first menu only after diagnostics and the timed hold finish.
  // If warnings/errors exist, OVERRIDE AND CONTINUE is selected by default.
  menu.begin();
  tft.update(true);
}

void loop() {
  // Hardware and protocol services remain independent of the visible screen.
  gps.update();
  can.update();
  obd.update();
  sim.update();
  webUi.update();

  // Physical encoder and phone controls use the same InputEvent pipeline.
  const InputEvent physicalEvent = encoder.poll();
  if (physicalEvent != InputEvent::None) {
    menu.handleInput(physicalEvent);
  }

  InputEvent remoteEvent = webUi.takeInputEvent();
  while (remoteEvent != InputEvent::None) {
    menu.handleInput(remoteEvent);
    remoteEvent = webUi.takeInputEvent();
  }

  // The menu publishes one renderer-independent UiFrame.
  // The TFT and WebSocket consume the same model without duplicating logic.
  menu.update();
  tft.update();

  delay(1);
}
