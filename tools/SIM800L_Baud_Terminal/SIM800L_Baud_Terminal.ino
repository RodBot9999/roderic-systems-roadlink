#include <Arduino.h>

// RoadLink SIM800L wiring, named from the ESP32 point of view.
constexpr uint8_t SIM_RX_PIN = 17;  // ESP32 RX  <- SIM800L TX
constexpr uint8_t SIM_TX_PIN = 16;  // ESP32 TX  -> SIM800L RX
constexpr uint8_t SIM_RST_PIN = 2;  // Active-low SIM800L reset
constexpr uint32_t USB_BAUD = 115200;

HardwareSerial sim800(1);

const uint32_t BAUD_RATES[] = {
    115200,
    9600,
    57600,
    38400,
    19200,
    4800,
    2400,
    1200,
};
constexpr size_t BAUD_RATE_COUNT =
    sizeof(BAUD_RATES) / sizeof(BAUD_RATES[0]);

uint32_t activeBaud = 115200;
String usbCommand;

void discardModemInput() {
  while (sim800.available()) {
    sim800.read();
  }
}

void startModemUart(uint32_t baud) {
  sim800.end();
  delay(50);

  // A disconnected UART RX must remain at the idle HIGH level.
  pinMode(SIM_RX_PIN, INPUT_PULLUP);
  sim800.setRxBufferSize(1024);
  sim800.begin(baud, SERIAL_8N1, SIM_RX_PIN, SIM_TX_PIN);
  activeBaud = baud;
  delay(80);
  discardModemInput();
}

void resetModem() {
  Serial.println(F("\r\n[RESET] Pulsing GPIO2 LOW for 200 ms..."));
  pinMode(SIM_RST_PIN, OUTPUT);
  digitalWrite(SIM_RST_PIN, HIGH);
  delay(100);
  digitalWrite(SIM_RST_PIN, LOW);
  delay(200);
  digitalWrite(SIM_RST_PIN, LOW);
  Serial.println(F("[RESET] Released. Waiting 3 seconds for UART boot output."));

  const uint32_t deadline = millis() + 3000;
  while (static_cast<int32_t>(deadline - millis()) > 0) {
    while (sim800.available()) {
      Serial.write(sim800.read());
    }
    delay(1);
  }
  Serial.println();
}

String collectResponse(uint32_t timeoutMs) {
  String response;
  response.reserve(256);
  const uint32_t startedMs = millis();

  while (millis() - startedMs < timeoutMs) {
    while (sim800.available()) {
      const char value = static_cast<char>(sim800.read());
      if (response.length() < 512) {
        response += value;
      }
    }

    if (response.indexOf("\r\nOK\r\n") >= 0 ||
        response.endsWith("\r\nOK") ||
        response == "OK") {
      break;
    }
    delay(1);
  }

  return response;
}

bool probeBaud(uint32_t baud) {
  startModemUart(baud);
  Serial.printf("[SCAN] %-6lu : ", static_cast<unsigned long>(baud));

  for (uint8_t attempt = 0; attempt < 3; ++attempt) {
    discardModemInput();
    sim800.print(F("AT\r"));
    const String response = collectResponse(700);

    if (response.indexOf("OK") >= 0) {
      Serial.println(F("RESPONSE FOUND"));
      Serial.print(F("[MODEM] "));
      Serial.println(response);
      return true;
    }
  }

  Serial.println(F("no response"));
  return false;
}

bool scanBaudRates() {
  Serial.println(F("\r\n========== SIM800L BAUD SCAN =========="));
  Serial.println(F("Sending AT three times at each common baud rate..."));

  for (size_t index = 0; index < BAUD_RATE_COUNT; ++index) {
    if (probeBaud(BAUD_RATES[index])) {
      Serial.printf(
          "\r\n[FOUND] SIM800L responded at %lu baud.\r\n",
          static_cast<unsigned long>(activeBaud));
      Serial.println(F("Terminal mode is now active at that baud."));
      Serial.println(F("Try: ATI, AT+IPR?, AT+CPIN?, AT+CSQ, AT+CREG?"));
      return true;
    }
  }

  startModemUart(115200);
  Serial.println(F("\r\n[NOT FOUND] No AT response at any scanned baud."));
  Serial.println(F("Terminal remains open at 115200 baud for manual testing."));
  Serial.println(F("A SIM card is NOT required for the modem to answer AT."));
  Serial.println(F("Check common ground, crossed TX/RX, module power state,"));
  Serial.println(F("PWRKEY startup, and voltage at the module during boot."));
  return false;
}

void printHelp() {
  Serial.println(F("\r\nLocal terminal commands:"));
  Serial.println(F("  /scan          Scan all common modem baud rates"));
  Serial.println(F("  /baud 115200   Select a baud rate manually"));
  Serial.println(F("  /reset         Pulse the SIM800L RST input"));
  Serial.println(F("  /help          Show this help"));
  Serial.println(F("\r\nAnything else is sent to the SIM800L with a CR ending."));
  Serial.println(F("Recommended first commands: AT, ATI, AT+IPR?, AT+CPIN?"));
}

void processUsbCommand(String command) {
  command.trim();
  if (command.isEmpty()) return;

  if (command.equalsIgnoreCase("/scan")) {
    scanBaudRates();
    return;
  }

  if (command.equalsIgnoreCase("/reset")) {
    resetModem();
    return;
  }

  if (command.equalsIgnoreCase("/help")) {
    printHelp();
    return;
  }

  if (command.startsWith("/baud ")) {
    const uint32_t requestedBaud = command.substring(6).toInt();
    if (requestedBaud == 0) {
      Serial.println(F("[ERROR] Example: /baud 115200"));
      return;
    }

    startModemUart(requestedBaud);
    Serial.printf(
        "[UART] Modem terminal changed to %lu baud.\r\n",
        static_cast<unsigned long>(activeBaud));
    return;
  }

  Serial.printf("[TX %lu] %s\r\n",
                static_cast<unsigned long>(activeBaud),
                command.c_str());
  sim800.print(command);
  sim800.print('\r');
}

void setup() {
  Serial.begin(USB_BAUD);
  usbCommand.reserve(128);
  delay(1200);

  Serial.println(F("\r\n========================================"));
  Serial.println(F("RoadLink SIM800L Baud Scanner + Terminal"));
  Serial.println(F("ESP32 RX=GPIO17, TX=GPIO16, RST=GPIO2"));
  Serial.println(F("USB Serial Monitor=115200 baud"));
  Serial.println(F("========================================"));

  pinMode(SIM_RX_PIN, INPUT_PULLUP);
  pinMode(SIM_RST_PIN, OUTPUT);
  digitalWrite(SIM_RST_PIN, HIGH);

  startModemUart(115200);
  resetModem();
  scanBaudRates();
  printHelp();
  Serial.println(F("\r\nType AT and press Enter."));
}

void loop() {
  while (sim800.available()) {
    Serial.write(sim800.read());
  }

  while (Serial.available()) {
    const char value = static_cast<char>(Serial.read());

    if (value == '\r' || value == '\n') {
      if (!usbCommand.isEmpty()) {
        processUsbCommand(usbCommand);
        usbCommand = "";
      }
      continue;
    }

    if (value == '\b' || value == 0x7F) {
      if (!usbCommand.isEmpty()) {
        usbCommand.remove(usbCommand.length() - 1);
      }
      continue;
    }

    if (usbCommand.length() < 120) {
      usbCommand += value;
    }
  }

  delay(1);
}
