#include <Arduino.h>

#include "AppConfig.h"
#include "AppState.h"
#include "StartupDiagnostics.h"
#include "EncoderInput.h"
#include "GpsService.h"
#include "CanService.h"
#include "ObdService.h"
#include "UiModel.h"
#include "WebUiService.h"
#include "TftRenderer.h"
#include "MenuSystem.h"

AppSettings settings;

EncoderInput encoder(
    Pins::ENCODER_CLK,
    Pins::ENCODER_DT,
    Pins::ENCODER_SW);

GpsService gps(2);
CanService can(Pins::CAN_CS, Pins::CAN_INT);
ObdService obd(can);
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
    settings,
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

void setup() {
  Serial.begin(AppConfig::USB_BAUD);
  delay(300);

  Serial.println();
  Serial.println(F("============================================================"));
  Serial.println(F("Roderic Systems RoadLink"));
  Serial.println(F("TFT + renderer-independent UI + CAN/OBD + GPS + WebSocket"));
  Serial.println(F("============================================================"));

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
  tft.setSystemCheck(0, "ROTARY INPUT", "OK", BootCheckState::Ok);

  ui.begin(settings.serialUiMirror);
  tft.setSystemCheck(1, "UI CORE", "OK", BootCheckState::Ok);

  gps.begin(
      AppConfig::GPS_BAUD,
      Pins::GPS_RX,
      Pins::GPS_TX,
      Pins::GPS_PPS);
  tft.setSystemCheck(2, "GPS RECEIVER", "OK", BootCheckState::Ok);

  obd.begin(settings.obdPollMs);
  tft.setSystemCheck(3, "OBD SERVICE", "READY", BootCheckState::Ok);

  diagnostics.clear();
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
  } else {
    tft.setSystemCheck(4, "CAN CONTROLLER", "OK", BootCheckState::Ok);
  }

  can.setSerialStreaming(settings.serialCanStreaming);
  menu.begin();

  // The HTTP page and WebSocket are hosted locally by the ESP32 SoftAP.
  settings.webUiEnabled = webUi.begin(settings.webUiEnabled);
  tft.setSystemCheck(
      5,
      "WEB SOCKET",
      settings.webUiEnabled ? "OK" : "OFF",
      settings.webUiEnabled ? BootCheckState::Ok : BootCheckState::Warning);

  tft.finishSystemCheck(
      canReady ? "BOOT COMPLETE / ALL CORE SYSTEMS READY"
               : "BOOT COMPLETE / CAN ERROR LOGGED");
  waitForBootPress(AppConfig::TFT_BOOT_HOLD_MS);

  // MenuSystem has already published the first UiFrame.
  tft.update(true);
}

void loop() {
  // Hardware and protocol services remain independent of the visible screen.
  gps.update();
  can.update();
  obd.update();
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
