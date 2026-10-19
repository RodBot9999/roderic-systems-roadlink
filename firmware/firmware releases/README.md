# RoadLink firmware releases

These folders contain OTA application images (`.ino.bin`), grouped by the day
the original files were last modified. V0.1 through V0.5 were labeled
retroactively on 2026-09-30; their binary contents were not changed and they
do not display those versions in About. V0.6 is the first release with its
version and release name embedded in the firmware.

| Version | Day | Release | Status |
| --- | --- | --- | --- |
| V0.1 | 2026-09-26 | Original RoadLink | Historical archive |
| V0.2 | 2026-09-27 | Sparky before Theme Studio | Historical archive |
| V0.3 | 2026-09-28 | Sparky Theme Studio | Historical archive |
| V0.4 | 2026-09-28 | Accidental device ID build | Do not flash; user reported missing features |
| V0.5 | 2026-09-30 | Complete Sparky with Theme Studio and device ID | Historical archive |
| V0.6 | 2026-09-30 | Sparky with version in the About header and device ID | Historical archive |
| V0.7 | 2026-09-30 | Sparky with a large version card and release name inside About | Current OTA release |

The `../latest firmware` folder contains only the current uploadable image.
Upload that `.ino.bin` file through the RoadLink OTA page's **Firmware**
section. The older files remain here for reference and recovery.

For the next release, update `FirmwareRelease.h`, compile the complete
`ROADLINK-UI-SPARKY` sketch, verify the ESP32 image, then run
`../Publish-FirmwareRelease.ps1`. The publisher archives the new image and
removes the previous image from `latest firmware` only after verifying that
the previous image exists in this archive.
