# SIM800L baud scanner and AT terminal

This standalone Arduino sketch tests only the RoadLink SIM800L UART and reset
connections. It does not initialize the TFT, CAN controller, GPS, Wi-Fi, or
RoadLink firmware.

## Pins

| Signal | Connection |
|---|---|
| ESP32 GPIO17 RX | SIM800L TX |
| ESP32 GPIO16 TX | SIM800L RX |
| ESP32 GPIO2 | SIM800L RST |
| Ground | Common ESP32/SIM800L/power-supply ground |

## Run it

1. Open `SIM800L_Baud_Terminal.ino` in Arduino IDE.
2. Select the same ESP32 board and COM port used by RoadLink.
3. Upload the sketch.
4. Open Serial Monitor at **115200 baud**.
5. Select **Newline** or **Both NL & CR** as the line ending.
6. Watch the automatic scan, then type `AT` and press Enter.

The scanner tries 115200, 9600, 57600, 38400, 19200, 4800, 2400, and 1200
baud. When it finds `OK`, it stays at that rate as an interactive terminal.

Local commands:

- `/scan` scans every listed baud again.
- `/baud 115200` changes the modem UART rate manually.
- `/reset` pulses the connected SIM800L RST input.
- `/help` prints the command list.

Useful modem commands:

- `AT` should return `OK`.
- `ATI` identifies the modem firmware.
- `AT+IPR?` reports the configured UART rate.
- `AT+CPIN?` reports SIM-card status.
- `AT+CSQ` reports signal strength.
- `AT+CREG?` reports GSM network registration.

A SIM card and antenna are not required for the module to answer plain `AT`.
If every baud fails, first verify that the module is actually running: a valid
VBAT voltage or power LED does not necessarily mean its baseband has been
started through PWRKEY. Also verify common ground, crossed UART directions, and
that the module supply does not dip while it boots.
