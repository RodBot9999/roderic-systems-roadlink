#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include "EncoderInput.h"
#include "UiModel.h"

class WebUiService {
public:
  WebUiService(
      uint16_t httpPort,
      uint16_t webSocketPort,
      const char* ssid,
      const char* password,
      UiModel& uiModel);

  bool begin(bool enabled);
  void update();

  bool setEnabled(bool enabled);
  bool enabled() const;
  bool accessPointReady() const;

  InputEvent takeInputEvent();
  String address() const;
  uint8_t connectedStations() const;
  uint8_t connectedWebSocketClients();

private:
  static constexpr uint8_t INPUT_QUEUE_SIZE = 16;

  void configureHttpRoutes();
  void configureWebSocket();
  bool startAccessPoint();
  void stopAccessPoint();

  void onWebSocketEvent(
      uint8_t clientNumber,
      WStype_t type,
      uint8_t* payload,
      size_t length);
  void queueInput(InputEvent event);
  void sendCurrentFrame(uint8_t clientNumber);
  void broadcastCurrentFrame(bool force = false);

  String frameJson() const;
  String jsonEscape(const String& value) const;
  const char* layoutLabel(UiLayout layout) const;

  WebServer httpServer_;
  WebSocketsServer webSocketServer_;
  const char* ssid_;
  const char* password_;
  UiModel& uiModel_;

  bool httpRoutesConfigured_ = false;
  bool webSocketConfigured_ = false;
  bool enabled_ = false;
  bool accessPointReady_ = false;

  InputEvent inputQueue_[INPUT_QUEUE_SIZE] = {};
  uint8_t inputQueueHead_ = 0;
  uint8_t inputQueueTail_ = 0;
  uint8_t inputQueueCount_ = 0;

  uint32_t lastBroadcastRevision_ = 0;
  uint32_t lastHeartbeatMs_ = 0;
};
