#include <Arduino.h>

#include "AppConfig.h"
#include "AppState.h"
#include "StartupDiagnostics.h"
#include "EncoderInput.h"
#include "GpsService.h"
#include "CanService.h"
#include "ObdService.h"
#include "A7670Service.h"
#include "SettingsStore.h"
#include "UiModel.h"
#include "StreamingController.h"
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
A7670Service sim(gps, obd, 1);
UiModel ui;
StartupDiagnostics diagnostics;
TftRenderer tft(
    ui,
    Pins::TFT_CS,
    Pins::TFT_DC,
    Pins::TFT_RST);

StreamingController streaming(can, obd, sim, settings, settingsStore);

MenuSystem menu(
    ui,
    can,
    obd,
    gps,
    sim,
    settings,
    settingsStore,
    diagnostics,
    streaming);

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
    tft.setSystemCheck(5, "A7670SA MODEM", "DISABLED", BootCheckState::Off);
  } else if (sim.snapshot().modemResponsive) {
    tft.setSystemCheck(5, "A7670SA MODEM", "DETECTED", BootCheckState::Ok);
  } else {
    diagnostics.reportWarning(
        ModuleId::Sim,
        skipRequested
            ? "A7670SA startup check skipped"
            : "A7670SA did not respond to an AT command",
        static_cast<int32_t>(sim.state()),
        static_cast<int32_t>(AppConfig::STARTUP_MODULE_PROBE_MS));
    tft.setSystemCheck(5, "A7670SA MODEM", "WARNING", BootCheckState::Warning);
  }

  return skipRequested;
}

void setup() {
  Serial.begin(AppConfig::USB_BAUD);
  delay(300);

  Serial.println();
  Serial.println(F("============================================================"));
  Serial.println(F("Roderic Systems RoadLink"));
  Serial.println(F("TFT + CAN/OBD + GPS + A7670SA telemetry"));
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

  Serial.println(F("[BOOT] Starting optional A7670SA UART"));
  sim.begin(
      AppConfig::MODEM_BAUD,
      Pins::MODEM_RX,
      Pins::MODEM_TX,
      settings.simEnabled,
      settings.simSendGps,
      settings.simSendObd,
      settings.simSendIntervalMs,
      settings.simServerIp,
      settings.simServerPort,
      settings.simAccessKey);
  streaming.begin();
  tft.setSystemCheck(
      5,
      "A7670SA MODEM",
      settings.simEnabled ? "WAIT" : "DISABLED",
      settings.simEnabled ? BootCheckState::Pending : BootCheckState::Off);

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
  static bool rebootPending = false;
  static uint32_t rebootStartedMs = 0;
  // Hardware and protocol services remain independent of the visible screen.
  gps.update();
  can.update();
  obd.update();
  sim.update();
  streaming.update();

  // Physical controls update the same configuration model used by future
  // semantic computer commands received through the LTE heartbeat.
  const InputEvent physicalEvent = encoder.poll();
  if (physicalEvent != InputEvent::None) {
    menu.handleInput(physicalEvent);
  }

  if (menu.takeRebootRequest()) {
    rebootPending = true;
    rebootStartedMs = millis();
    sim.requestSoftwareRestart();
    Serial.println(F("[REBOOT] Settling AT command, requesting modem reset"));
  }

  // The menu publishes the local TFT frame. Remote configuration never
  // duplicates or navigates the screen.
  menu.update();
  tft.update();

  // Non-blocking: AT operator selection can take up to 180 s; HTTP service
  // cleanup up to 120 s. Allow both plus the reset command before fallback.
  // A missing/unresponsive modem must not prevent the ESP32 reboot.
  if (rebootPending && (sim.softwareRestartFinished() ||
      millis() - rebootStartedMs >= 315000UL)) {
    Serial.println(sim.softwareRestartSucceeded()
        ? F("[REBOOT] Modem acknowledged CRESET; restarting ESP32")
        : F("[REBOOT] Modem reset unconfirmed; restarting ESP32 anyway"));
    Serial.flush();
    delay(100);
    ESP.restart();
  }

  delay(1);
}
