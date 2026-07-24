#include "WebUiService.h"
#include "AppConfig.h"
#include <WiFi.h>

namespace {
const char WEB_PAGE[] PROGMEM = R"RODBOTHTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
  <meta name="theme-color" content="#0d1117">
  <title>Roderic Systems RoadLink</title>
  <style>
    :root {
      color-scheme: dark;
      font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, "Liberation Mono", monospace;
      --bg:#020607; --panel:#071013; --panel2:#0d1a1e; --line:#294047;
      --text:#f4fbfc; --muted:#72868b; --accent:#b3dbe3; --ok:#45d483;
      --danger:#ff5f56; --selected:#19353b;
    }
    * { box-sizing:border-box; }
    body { margin:0; background:var(--bg); color:var(--text); min-height:100vh; }
    main { width:min(100%,760px); margin:0 auto; padding:18px 14px 110px; }
    header { margin-bottom:14px; }
    h1 { margin:0; font-size:1.35rem; }
    #crumb { color:var(--muted); margin-top:5px; font-size:.88rem; }
    #subtitle { color:#c9d1d9; margin-top:8px; }
    .panel { background:var(--panel); border:1px solid var(--line); border-radius:4px; overflow:hidden; margin-top:12px; }
    .panel-title { padding:10px 13px; color:var(--muted); font-size:.78rem; text-transform:uppercase; letter-spacing:.08em; border-bottom:1px solid var(--line); }
    #items { list-style:none; margin:0; padding:0; }
    .item { display:flex; justify-content:space-between; gap:12px; padding:13px 14px; border-bottom:1px solid var(--line); }
    .item:last-child { border-bottom:0; }
    .item.selected { background:var(--selected); box-shadow:inset 4px 0 var(--accent); }
    .item.disabled { opacity:.45; }
    .item.destructive { color:#ffb4ae; }
    .item-value { color:var(--muted); text-align:right; }
    #fields { display:grid; grid-template-columns:minmax(120px,.8fr) minmax(0,1.2fr); }
    .field { display:contents; }
    .field-label,.field-value { padding:11px 13px; border-bottom:1px solid var(--line); }
    .field-label { color:var(--muted); }
    .field-value { text-align:right; font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace; overflow-wrap:anywhere; }
    #status { margin-top:12px; padding:11px 13px; border:1px solid var(--line); border-radius:4px; color:var(--muted); background:var(--panel); }
    #connection.ok { color:var(--ok); }
    #connection.bad { color:var(--danger); }
    .controls { position:fixed; left:0; right:0; bottom:0; padding:10px max(10px,env(safe-area-inset-right)) calc(10px + env(safe-area-inset-bottom)) max(10px,env(safe-area-inset-left)); background:#0d1117ee; backdrop-filter:blur(12px); border-top:1px solid var(--line); }
    .control-grid { width:min(100%,760px); margin:0 auto; display:grid; grid-template-columns:1fr 1.15fr 1fr .9fr; gap:8px; }
    button { min-height:54px; border-radius:3px; border:1px solid #484f58; background:var(--panel2); color:var(--text); font-weight:750; font-size:.95rem; }
    button:active { transform:scale(.97); background:#30363d; }
    button.back { color:#ffcc66; }
    .hidden { display:none !important; }
    @media (max-width:480px) {
      #fields { grid-template-columns:1fr 1fr; }
      .control-grid { grid-template-columns:1fr 1.2fr 1fr; }
      button.back { grid-column:1 / -1; min-height:44px; }
    }
  </style>
</head>
<body>
<main>
  <header>
    <h1 id="title">RODERIC SYSTEMS // CAN DIAGNOSTICS</h1>
    <div id="crumb">Connecting to ESP32...</div>
    <div id="subtitle"></div>
  </header>

  <section id="dataPanel" class="panel hidden">
    <div class="panel-title">Live data</div>
    <div id="fields"></div>
  </section>

  <section id="optionsPanel" class="panel hidden">
    <div class="panel-title">Options</div>
    <ul id="items"></ul>
  </section>

  <div id="status">
    <span id="connection" class="bad">WebSocket disconnected</span>
  </div>
</main>

<div class="controls">
  <div class="control-grid">
    <button onclick="sendInput('left')">&#9664; LEFT</button>
    <button onclick="sendInput('select')">SELECT</button>
    <button onclick="sendInput('right')">RIGHT &#9654;</button>
    <button class="back" onclick="sendInput('back')">BACK</button>
  </div>
</div>

<script>
let socket = null;
let reconnectTimer = null;
let lastFrameAt = 0;

function connectSocket() {
  clearTimeout(reconnectTimer);
  const url = `ws://${location.hostname}:81/`;
  socket = new WebSocket(url);

  socket.onopen = () => {
    setConnection(true, 'Live WebSocket connected');
    socket.send('state');
  };

  socket.onmessage = event => {
    try {
      const frame = JSON.parse(event.data);
      lastFrameAt = Date.now();
      renderFrame(frame);
    } catch (error) {
      console.error('Invalid frame', error);
    }
  };

  socket.onerror = () => setConnection(false, 'WebSocket error');
  socket.onclose = () => {
    setConnection(false, 'Disconnected; reconnecting...');
    reconnectTimer = setTimeout(connectSocket, 1200);
  };
}

function setConnection(ok, text) {
  const element = document.getElementById('connection');
  element.className = ok ? 'ok' : 'bad';
  element.textContent = text;
}

function renderFrame(frame) {
  document.getElementById('title').textContent = frame.title || 'RODERIC SYSTEMS // CAN DIAGNOSTICS';
  document.getElementById('crumb').textContent = frame.breadcrumb || frame.layout || '';
  document.getElementById('subtitle').textContent = frame.subtitle || '';

  const fields = document.getElementById('fields');
  fields.replaceChildren();
  (frame.fields || []).forEach(field => {
    const row = document.createElement('div');
    row.className = 'field';
    const label = document.createElement('div');
    label.className = 'field-label';
    label.textContent = field.label;
    const value = document.createElement('div');
    value.className = 'field-value';
    value.textContent = field.value;
    row.append(label, value);
    fields.appendChild(row);
  });
  document.getElementById('dataPanel').classList.toggle('hidden', !(frame.fields || []).length);

  const items = document.getElementById('items');
  items.replaceChildren();
  (frame.items || []).forEach((item, index) => {
    const li = document.createElement('li');
    li.className = 'item';
    if (index === frame.selectedIndex) li.classList.add('selected');
    if (!item.enabled) li.classList.add('disabled');
    if (item.destructive) li.classList.add('destructive');

    const label = document.createElement('span');
    label.textContent = item.label;
    const value = document.createElement('span');
    value.className = 'item-value';
    value.textContent = item.value || '';
    li.append(label, value);
    items.appendChild(li);
  });
  document.getElementById('optionsPanel').classList.toggle('hidden', !(frame.items || []).length);

  const details = [frame.status || '', `revision ${frame.revision}`, `uptime ${Math.floor((frame.uptimeMs || 0)/1000)} s`]
    .filter(Boolean).join('  •  ');
  document.getElementById('status').innerHTML = `<span id="connection" class="ok">Live WebSocket connected</span><br>${escapeHtml(details)}`;
}

function sendInput(eventName) {
  if (!socket || socket.readyState !== WebSocket.OPEN) {
    setConnection(false, 'Not connected');
    return;
  }
  socket.send(eventName);
}

function escapeHtml(value) {
  return String(value).replace(/[&<>'"]/g, char => ({'&':'&amp;','<':'&lt;','>':'&gt;',"'":'&#39;','"':'&quot;'}[char]));
}

connectSocket();
</script>
</body>
</html>
)RODBOTHTML";
}

WebUiService::WebUiService(
    uint16_t httpPort,
    uint16_t webSocketPort,
    const char* ssid,
    const char* password,
    UiModel& uiModel)
  : httpServer_(httpPort),
    webSocketServer_(webSocketPort),
    ssid_(ssid),
    password_(password),
    uiModel_(uiModel) {}

bool WebUiService::begin(bool enabled) {
  configureHttpRoutes();
  configureWebSocket();
  return setEnabled(enabled);
}

void WebUiService::update() {
  if (!enabled_ || !accessPointReady_) return;

  httpServer_.handleClient();
  webSocketServer_.loop();
  broadcastCurrentFrame(false);

  if (millis() - lastHeartbeatMs_ >= AppConfig::WEB_HEARTBEAT_MS) {
    lastHeartbeatMs_ = millis();
    webSocketServer_.broadcastPing();
  }
}

bool WebUiService::setEnabled(bool enabled) {
  if (enabled == enabled_) return enabled_;
  if (enabled) return startAccessPoint();
  stopAccessPoint();
  return false;
}

bool WebUiService::enabled() const { return enabled_; }
bool WebUiService::accessPointReady() const { return accessPointReady_; }

InputEvent WebUiService::takeInputEvent() {
  if (inputQueueCount_ == 0) return InputEvent::None;

  const InputEvent event = inputQueue_[inputQueueTail_];
  inputQueueTail_ = (inputQueueTail_ + 1) % INPUT_QUEUE_SIZE;
  inputQueueCount_--;
  return event;
}

String WebUiService::address() const {
  return accessPointReady_
      ? String("http://") + WiFi.softAPIP().toString()
      : String("OFF");
}

uint8_t WebUiService::connectedStations() const {
  return accessPointReady_ ? WiFi.softAPgetStationNum() : 0;
}

uint8_t WebUiService::connectedWebSocketClients() {
  return enabled_ ? static_cast<uint8_t>(webSocketServer_.connectedClients()) : 0;
}

void WebUiService::configureHttpRoutes() {
  if (httpRoutesConfigured_) return;

  httpServer_.on("/", HTTP_GET, [this]() {
    httpServer_.send_P(200, "text/html", WEB_PAGE);
  });

  httpServer_.on("/api/state", HTTP_GET, [this]() {
    httpServer_.sendHeader("Cache-Control", "no-store");
    httpServer_.send(200, "application/json", frameJson());
  });

  httpServer_.onNotFound([this]() {
    httpServer_.send(404, "text/plain", "RoadLink endpoint not found");
  });

  httpRoutesConfigured_ = true;
}

void WebUiService::configureWebSocket() {
  if (webSocketConfigured_) return;

  webSocketServer_.onEvent(
      [this](uint8_t number, WStype_t type, uint8_t* payload, size_t length) {
        onWebSocketEvent(number, type, payload, length);
      });
  webSocketServer_.enableHeartbeat(15000, 3000, 2);
  webSocketConfigured_ = true;
}

bool WebUiService::startAccessPoint() {
  configureHttpRoutes();
  configureWebSocket();

  WiFi.mode(WIFI_AP);
  accessPointReady_ = WiFi.softAP(ssid_, password_);
  if (!accessPointReady_) {
    enabled_ = false;
    WiFi.mode(WIFI_OFF);
    Serial.println(F("[Web UI] Failed to start access point"));
    return false;
  }

  httpServer_.begin();
  webSocketServer_.begin();
  enabled_ = true;
  lastBroadcastRevision_ = 0;

  Serial.println();
  Serial.println(F("[Web UI] HTTP + WebSocket started"));
  Serial.print(F("[Web UI] Network: "));
  Serial.println(ssid_);
  Serial.print(F("[Web UI] Address: "));
  Serial.println(address());
  Serial.print(F("[Web UI] WebSocket port: "));
  Serial.println(AppConfig::WEB_SOCKET_PORT);
  return true;
}

void WebUiService::stopAccessPoint() {
  if (accessPointReady_) {
    webSocketServer_.disconnect();
    webSocketServer_.close();
    httpServer_.stop();
    WiFi.softAPdisconnect(true);
  }

  WiFi.mode(WIFI_OFF);
  enabled_ = false;
  accessPointReady_ = false;
  inputQueueHead_ = 0;
  inputQueueTail_ = 0;
  inputQueueCount_ = 0;
  Serial.println(F("[Web UI] OFF"));
}

void WebUiService::onWebSocketEvent(
    uint8_t clientNumber,
    WStype_t type,
    uint8_t* payload,
    size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      Serial.print(F("[WebSocket] Client connected: "));
      Serial.println(clientNumber);
      sendCurrentFrame(clientNumber);
      break;

    case WStype_DISCONNECTED:
      Serial.print(F("[WebSocket] Client disconnected: "));
      Serial.println(clientNumber);
      break;

    case WStype_TEXT: {
      String message;
      message.reserve(length);
      for (size_t index = 0; index < length; ++index) {
        message += static_cast<char>(payload[index]);
      }
      message.trim();
      message.toLowerCase();

      if (message == "left") queueInput(InputEvent::RotateLeft);
      else if (message == "right") queueInput(InputEvent::RotateRight);
      else if (message == "select") queueInput(InputEvent::Press);
      else if (message == "back") queueInput(InputEvent::Back);
      else if (message == "state") sendCurrentFrame(clientNumber);
      break;
    }

    default:
      break;
  }
}

void WebUiService::queueInput(InputEvent event) {
  if (event == InputEvent::None) return;

  if (inputQueueCount_ >= INPUT_QUEUE_SIZE) {
    inputQueueTail_ = (inputQueueTail_ + 1) % INPUT_QUEUE_SIZE;
    inputQueueCount_--;
  }

  inputQueue_[inputQueueHead_] = event;
  inputQueueHead_ = (inputQueueHead_ + 1) % INPUT_QUEUE_SIZE;
  inputQueueCount_++;
}

void WebUiService::sendCurrentFrame(uint8_t clientNumber) {
  String json = frameJson();
  webSocketServer_.sendTXT(clientNumber, json);
}

void WebUiService::broadcastCurrentFrame(bool force) {
  const uint32_t revision = uiModel_.revision();
  if (!force && revision == lastBroadcastRevision_) return;

  lastBroadcastRevision_ = revision;
  String json = frameJson();
  webSocketServer_.broadcastTXT(json);
}

String WebUiService::frameJson() const {
  const UiFrame& frame = uiModel_.frame();

  String json;
  json.reserve(2400);
  json += F("{\"revision\":");
  json += String(uiModel_.revision());
  json += F(",\"uptimeMs\":");
  json += String(millis());
  json += F(",\"layout\":\"");
  json += layoutLabel(frame.layout);
  json += F("\",\"title\":\"");
  json += jsonEscape(frame.title);
  json += F("\",\"breadcrumb\":\"");
  json += jsonEscape(frame.breadcrumb);
  json += F("\",\"subtitle\":\"");
  json += jsonEscape(frame.subtitle);
  json += F("\",\"status\":\"");
  json += jsonEscape(frame.status);
  json += F("\",\"canGoBack\":");
  json += frame.canGoBack ? F("true") : F("false");
  json += F(",\"selectedIndex\":");
  json += String(frame.selectedIndex);

  json += F(",\"items\":[");
  for (uint8_t index = 0; index < frame.itemCount; ++index) {
    if (index) json += ',';
    json += F("{\"label\":\"");
    json += jsonEscape(frame.items[index].label);
    json += F("\",\"value\":\"");
    json += jsonEscape(frame.items[index].value);
    json += F("\",\"enabled\":");
    json += frame.items[index].enabled ? F("true") : F("false");
    json += F(",\"destructive\":");
    json += frame.items[index].destructive ? F("true") : F("false");
    json += '}';
  }
  json += ']';

  json += F(",\"fields\":[");
  for (uint8_t index = 0; index < frame.fieldCount; ++index) {
    if (index) json += ',';
    json += F("{\"label\":\"");
    json += jsonEscape(frame.fields[index].label);
    json += F("\",\"value\":\"");
    json += jsonEscape(frame.fields[index].value);
    json += F("\"}");
  }
  json += F("]}");
  return json;
}

String WebUiService::jsonEscape(const String& value) const {
  String escaped;
  escaped.reserve(value.length() + 8);

  for (size_t index = 0; index < value.length(); ++index) {
    const char character = value[index];
    switch (character) {
      case '\\': escaped += F("\\\\"); break;
      case '"':  escaped += F("\\\""); break;
      case '\n': escaped += F("\\n"); break;
      case '\r': break;
      case '\t': escaped += F("\\t"); break;
      default:   escaped += character; break;
    }
  }
  return escaped;
}

const char* WebUiService::layoutLabel(UiLayout layout) const {
  switch (layout) {
    case UiLayout::Menu:   return "menu";
    case UiLayout::Detail: return "detail";
    case UiLayout::Alert:  return "alert";
  }
  return "unknown";
}
