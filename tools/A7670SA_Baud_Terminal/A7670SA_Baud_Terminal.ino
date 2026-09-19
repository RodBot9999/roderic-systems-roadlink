#include <Arduino.h>

// RoadLink A7670SA wiring, named from the ESP32 point of view.
constexpr uint8_t MODEM_RX_PIN = 17;  // ESP32 RX  <- A7670SA TX
constexpr uint8_t MODEM_TX_PIN = 16;  // ESP32 TX  -> A7670SA RX
constexpr uint32_t USB_BAUD = 115200;

HardwareSerial modem(1);

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
  while (modem.available()) {
    modem.read();
  }
}

void startModemUart(uint32_t baud) {
  modem.end();
  delay(50);

  // A disconnected UART RX must remain at the idle HIGH level.
  pinMode(MODEM_RX_PIN, INPUT_PULLUP);
  modem.setRxBufferSize(1024);
  modem.begin(baud, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);
  activeBaud = baud;
  delay(80);
  discardModemInput();
}

String collectResponse(uint32_t timeoutMs) {
  String response;
  response.reserve(256);
  const uint32_t startedMs = millis();

  while (millis() - startedMs < timeoutMs) {
    while (modem.available()) {
      const char value = static_cast<char>(modem.read());
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
    modem.print(F("AT\r"));
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
  Serial.println(F("\r\n========== A7670SA BAUD SCAN =========="));
  Serial.println(F("Sending AT three times at each common baud rate..."));

  for (size_t index = 0; index < BAUD_RATE_COUNT; ++index) {
    if (probeBaud(BAUD_RATES[index])) {
      Serial.printf(
          "\r\n[FOUND] A7670SA responded at %lu baud.\r\n",
          static_cast<unsigned long>(activeBaud));
      Serial.println(F("Terminal mode is now active at that baud."));
      Serial.println(F("Try: ATI, AT+IPR?, AT+CPIN?, AT+CSQ, AT+CEREG?"));
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
  Serial.println(F("  /help          Show this help"));
  Serial.println(F("\r\nAnything else is sent to the A7670SA with a CR ending."));
  Serial.println(F("Recommended: AT, ATI, AT+CPIN?, AT+CEREG?, AT+CSQ"));
}

void processUsbCommand(String command) {
  command.trim();
  if (command.isEmpty()) return;

  if (command.equalsIgnoreCase("/scan")) {
    scanBaudRates();
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
  modem.print(command);
  modem.print('\r');
}

void setup() {
  Serial.begin(USB_BAUD);
  usbCommand.reserve(128);
  delay(1200);

  Serial.println(F("\r\n========================================"));
  Serial.println(F("RoadLink A7670SA Baud Scanner + Terminal"));
  Serial.println(F("ESP32 RX=GPIO17, TX=GPIO16 (UART only)"));
  Serial.println(F("USB Serial Monitor=115200 baud"));
  Serial.println(F("========================================"));

  pinMode(MODEM_RX_PIN, INPUT_PULLUP);

  startModemUart(115200);
  scanBaudRates();
  printHelp();
  Serial.println(F("\r\nType AT and press Enter."));
}

void loop() {
  while (modem.available()) {
    Serial.write(modem.read());
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
