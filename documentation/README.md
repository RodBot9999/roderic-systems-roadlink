# RoadLink technical documentation

> **Languages:** Development will be written first in [Spanish](spanish/README.md), since it is the language used for the class. This English version will be synchronized afterward.

This directory contains the technical documentation for RoadLink. The documentation is divided by subsystem so that each part can be studied, tested, and evaluated independently. A complete system document will later bring these parts together and explain RoadLink as a whole.

## Purpose

RoadLink is an embedded vehicle-communication project. Its central controller receives information from the vehicle and attached sensors, presents useful information to the user, and uses a cellular connection to transmit data outside the vehicle.

The documents in this directory are intended to record:

- the purpose of each module;
- why the component was selected;
- how it connects to the rest of RoadLink;
- the tests performed during development;
- changes between early prototypes and the current design;
- limitations, problems, and future improvements.

## Documentation map

| Subsystem | Document | Status |
| --- | --- | --- |
| ESP32 controller | [ESP32 controller](modules/esp32-controller.md) | Initial draft |
| Cellular communication | [Cellular communication](modules/cellular-communication.md) | Initial draft |
| CAN bus interface | `modules/can-bus-interface.md` | Planned |
| GPS | `modules/gps.md` | Planned |
| TFT display and rotary encoder | `modules/human-interface.md` | Planned |
| PCB, power supplies, and connections | `hardware/pcb-power-and-connections.md` | Planned |
| Complete RoadLink system | `roadlink-system-overview.md` | Planned after the module documents |

## Media organization

Photographs, diagrams, screenshots, and test evidence belong in `assets/images/`. File names should describe the subject and development stage, for example:

```text
assets/images/
├── esp32-sim800l-test-setup.jpg
├── sim800l-sms-test-result.jpg
├── sim800l-tcp-test-setup.jpg
└── esp32-a7670sa-test-setup.jpg
```

Each image should be referenced from the relevant module document and accompanied by a short caption explaining what the image demonstrates. Images should support the technical explanation rather than serve only as decoration.

## Documentation status labels

- **Initial draft:** the document has a useful structure and preliminary content but still needs project-specific measurements, photographs, or confirmation.
- **In progress:** project evidence is being added and checked.
- **Verified:** the description has been compared with the final hardware, firmware, schematics, and test results.

## Items that still need confirmation

The first drafts intentionally avoid guessing details that have not yet been recorded. The following information should be confirmed as the documentation develops:

- exact ESP32 board or module used;
- exact pins and serial ports assigned to each peripheral;
- supply voltages and measured current requirements;
- chronology and results of the SIM800L SMS and TCP/IP tests;
- reason for replacing the SIM800L with the A7670SA;
- carrier, SIM, antenna, and network configuration used during testing;
- current A7670SA test results and final integration status.
