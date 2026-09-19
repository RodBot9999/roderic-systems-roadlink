# A7670SA baud scanner and AT terminal

This standalone Arduino sketch tests only the RoadLink A7670SA UART connection.
It does not initialize the TFT, CAN controller, GPS, Wi-Fi, or
RoadLink firmware.

## Pins

| Signal | Connection |
|---|---|
| ESP32 GPIO17 RX | A7670SA TX |
| ESP32 GPIO16 TX | A7670SA RX |
| Ground | Common ESP32/A7670SA/power-supply ground |

## Run it

1. Open `A7670SA_Baud_Terminal.ino` in Arduino IDE.
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
- `/help` prints the command list.

Useful modem commands:

- `AT` should return `OK`.
- `ATI` identifies the modem firmware.
- `AT+IPR?` reports the configured UART rate.
- `AT+CPIN?` reports SIM-card status.
- `AT+CSQ` reports signal strength.
- `AT+CEREG?` reports LTE/EPS network registration.
- `AT+CGDCONT?` reports configured PDP contexts and APNs.
- `AT+CGACT?` reports active PDP contexts.

A SIM card and antenna are not required for the module to answer plain `AT`.
If every baud fails, first verify that the module is actually running: a valid
input voltage or power LED does not necessarily mean its baseband has started.
Because reset and PWRKEY are not connected to the ESP32, power-on behavior must
be provided by the breakout board. Also verify common ground, crossed UART
directions, and that the module supply does not dip while it boots.
