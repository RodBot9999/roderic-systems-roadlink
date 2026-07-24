#pragma once

#include <Arduino.h>

namespace Pins {
constexpr uint8_t ENCODER_CLK = 25;
constexpr uint8_t ENCODER_DT  = 26;
constexpr uint8_t ENCODER_SW  = 27;

constexpr uint8_t GPS_PPS = 34;
constexpr uint8_t GPS_RX  = 35;  // ESP32 RX <- GPS TX
constexpr uint8_t GPS_TX  = 32;  // ESP32 TX -> GPS RX

// GPIO labels below are from the ESP32 perspective:
// GPIO16 drives the SIM800L RX pin; GPIO17 receives from the SIM800L TX pin.
constexpr uint8_t SIM_RST = 2;
constexpr uint8_t SIM_RX  = 17;  // ESP32 RX <- SIM800L TX
constexpr uint8_t SIM_TX  = 16;  // ESP32 TX -> SIM800L RX

// Final MCP2515 wiring after physically flipping the module.
constexpr uint8_t CAN_CS   = 23;
constexpr uint8_t CAN_MISO = 19; // ESP32 <- MCP2515 SO
constexpr uint8_t CAN_MOSI = 18; // ESP32 -> MCP2515 SI
constexpr uint8_t CAN_SCK  = 5;
constexpr uint8_t CAN_INT  = 4;

// 2.4-inch 240x320 SPI TFT, used in landscape mode.
// GPIO12/GPIO14 are the confirmed working control pins on the assembled unit.
constexpr uint8_t TFT_CS  = 12;
constexpr uint8_t TFT_DC  = 14;
constexpr uint8_t TFT_RST = 15;
}

namespace AppConfig {
constexpr uint32_t USB_BAUD = 115200;
constexpr uint32_t GPS_BAUD = 9600;
constexpr uint32_t SIM_BAUD = 115200;

constexpr uint8_t TFT_ROTATION = 3;
constexpr uint32_t TFT_SPI_HZ = 27000000;
constexpr uint16_t TFT_BOOT_HOLD_MS = 3000;

constexpr char WEB_UI_SSID[] = "RodBot-Scanner";
constexpr char WEB_UI_PASSWORD[] = "rodtracklog";
constexpr uint16_t WEB_HTTP_PORT = 80;
constexpr uint16_t WEB_SOCKET_PORT = 81;
constexpr uint32_t WEB_HEARTBEAT_MS = 2000;

// Change only these carrier values for the installed SIM card.
// The receiver IP, port, and access key are edited on the TFT and saved in NVS.
constexpr char SIM_APN[] = "YOUR_CARRIER_APN";
constexpr char SIM_APN_USER[] = "";
constexpr char SIM_APN_PASSWORD[] = "";
constexpr uint32_t SIM_SEND_INTERVAL_DEFAULT_MS = 10000;
constexpr uint32_t SIM_SEND_INTERVAL_MIN_MS = 5000;
constexpr uint32_t SIM_COMMAND_TIMEOUT_MS = 5000;
constexpr uint32_t SIM_HTTP_TIMEOUT_MS = 70000;
constexpr uint32_t SIM_RETRY_DELAY_MS = 5000;
constexpr uint16_t SIM_RESET_PULSE_MS = 150;
constexpr uint16_t SIM_BOOT_WAIT_MS = 3500;
constexpr uint32_t STARTUP_MODULE_PROBE_MS = 5000;

constexpr uint8_t ENCODER_STEPS_PER_DETENT = 2;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 40;

constexpr uint8_t CAN_MAX_FRAMES_PER_LOOP = 48;
constexpr uint8_t CAN_MAX_TRACKED_IDS = 64;
constexpr uint8_t CAN_DIAGNOSTIC_QUEUE_SIZE = 24;
constexpr uint32_t CAN_ACTIVE_TIMEOUT_MS = 1500;

constexpr uint16_t UI_REFRESH_DEFAULT_MS = 150;
constexpr uint16_t OBD_POLL_DEFAULT_MS = 120;
constexpr uint16_t OBD_RESPONSE_TIMEOUT_MS = 700;
constexpr uint16_t OBD_DISCOVERY_WINDOW_MS = 900;
constexpr uint16_t OBD_SUPPORTED_WINDOW_MS = 450;
constexpr uint8_t OBD_MAX_ECUS = 8;
constexpr uint8_t OBD_MAX_DTCS = 20;
constexpr uint16_t OBD_ISOTP_BUFFER_SIZE = 128;
}
