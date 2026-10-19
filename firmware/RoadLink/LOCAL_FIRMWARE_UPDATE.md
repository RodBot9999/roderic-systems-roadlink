# Local firmware updates

RoadLink now has an independent Wi-Fi firmware update service. It does not use
the A7670SA or cellular data. The existing CAN, GPS, modem, streaming, and TFT
menu services keep their current setup and loop behavior.

## One-time installation

1. Connect USB once and select **ESP32 Dev Module** (or the exact matching
   ESP32 board) in Arduino IDE. Keep an OTA-capable partition scheme. The
   installed ESP32 core's **Default 4MB with spiffs** scheme has two 1.25 MiB
   application slots and works with the current build. **No FS 4MB (2MB APP
   x2)** gives each slot about 1.94 MiB if the board has at least 4 MB flash.
   Do not select a **No OTA** or **Huge APP** scheme.
2. Upload the updated sketch over USB. This first USB upload is necessary:
   firmware already on the board cannot gain OTA capability wirelessly.
3. Open Serial Monitor at 115200 baud. After the normal RoadLink startup, note
   the `RoadLink-OTA-XXXXXX` Wi-Fi name and its 20-character password. They
   remain the same across reboots and future OTA updates. Store the password
   somewhere you can retrieve after the USB port is enclosed.

The updater checks for a second app partition before starting its access point.
It stores only its password in a separate `roadlink_ota` NVS namespace. The
existing RoadLink settings are unchanged.

## Later updates without USB

1. Compile the new RoadLink sketch with the same board and partition scheme.
   In Arduino IDE use **Sketch > Export Compiled Binary**. Use the application
   `RoadLink.ino.bin`, not a bootloader, partition-table, or merged flash image.
2. Park the vehicle and keep power stable. Connect your computer or phone to
   `RoadLink-OTA-XXXXXX` using the password recorded during the first upload.
   The computer may say this Wi-Fi network has no internet; stay connected.
3. Open `http://192.168.4.1/update` in a browser. Log in with username
   `roadlink` and the same password as the Wi-Fi network.
4. Select the application `.bin` and upload it. Wait for the success response
   and RoadLink reboot before disconnecting power. Reconnect to the same Wi-Fi
   name after the reboot if you want to verify that the service restarted.

The update page is available while RoadLink is powered, so the USB port does
not need to be reachable for later updates. Wi-Fi is local to RoadLink; it
does not connect to a home router or spend cellular data. Firmware upload can
briefly occupy the ESP32, so update only when the vehicle is stationary.

## Storage and future cellular transport

The built-in ESP32 updater writes the incoming image directly into the
inactive application partition. No filesystem partition or full-image RAM
buffer is needed. An interrupted upload leaves the currently running image
selected. Do not rely on automatic rollback after a *successfully installed*
image fails to boot; keep USB recovery access until that behavior has been
verified on the physical board.

The future A7670SA transport should download a versioned image in chunks and
write those chunks with the ESP32 `Update.begin` / `Update.write` / `Update.end`
API to the same inactive partition. The local Wi-Fi service does not issue AT
commands, and no cellular update path is enabled yet.

The firmware image must fit in **each** OTA app slot. Arduino IDE's build
output reports the current image size and maximum for the selected partition
scheme. If the access point does not appear, check Serial Monitor for an
`[OTA]` message, especially the inactive-partition and Wi-Fi startup checks.
