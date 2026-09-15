# ESP32 controller

**Document status:** Initial draft  
**Subsystem:** Main controller and firmware platform

## 1. Role in RoadLink

The ESP32 is the main controller, or “brain,” of RoadLink. It coordinates the other electronic modules and runs the firmware that defines the system's behavior. Its responsibilities include collecting information, processing it, updating the user interface, and requesting that the cellular module send data to an external destination.

In the complete system, the ESP32 is expected to interact with:

- the CAN bus interface, which provides information from the vehicle;
- the GPS receiver, which provides position and movement data;
- the cellular module, which provides wide-area communication;
- the TFT display, which presents information to the user;
- the rotary encoder and its push button, which receive user input;
- the PCB's power and signal-conditioning circuits.

The ESP32 does not perform every function by itself. Instead, it acts as the coordinator between specialized modules. This separation makes it possible to test a peripheral independently before integrating it into the full RoadLink system.

## 2. Why an ESP32 is suitable

The ESP32 family is well suited to this type of prototype because it combines a programmable microcontroller with several communication peripherals. RoadLink can use these hardware interfaces to communicate with multiple modules without requiring a separate processor for each one.

Relevant capabilities include:

- UART serial communication for cellular and GPS modules;
- SPI communication for devices such as a TFT display and CAN controller;
- GPIO pins for buttons, encoder signals, reset lines, and status signals;
- timers and interrupts for responsive input and periodic tasks;
- enough processing capability to parse messages and control several subsystems;
- non-volatile flash storage for the RoadLink firmware and configuration.

The exact ESP32 board/module and the interfaces used in the final prototype will be added after they are checked against the hardware and firmware.

## 3. Position in the system

The following logical diagram shows the ESP32's central role. It describes information flow, not final pin assignments.

```text
                  ┌──────────────────┐
Vehicle CAN bus ─►│ CAN bus interface│──┐
                  └──────────────────┘  │
                                        ▼
┌──────────────┐                  ┌───────────┐                  ┌─────────────────┐
│ GPS receiver │─────────────────►│   ESP32   │◄────────────────►│ Cellular module │
└──────────────┘                  │ controller│                  └─────────────────┘
                                  └─────┬─────┘
                                        │
                       ┌────────────────┴───────────────┐
                       ▼                                ▼
                ┌─────────────┐                 ┌──────────────┐
                │ TFT display │                 │Rotary encoder│
                └─────────────┘                 └──────────────┘
```

Power-supply connections and voltage domains are not shown here because they will be documented in the PCB and power-supply document.

## 4. Firmware responsibilities

The firmware should keep communication and user-interface tasks organized so that a slow operation in one module does not stop the entire system. At a high level, its operating cycle is:

1. initialize the ESP32 and attached modules;
2. check whether each required module responds;
3. receive vehicle and GPS data;
4. validate and store the latest useful values;
5. update the display and respond to encoder input;
6. prepare the information that must be transmitted;
7. exchange commands and data with the cellular module;
8. detect communication errors and retry or report them safely.

The final document should identify the actual firmware files or functions responsible for these tasks once the code is added to the repository.

## 5. Development and testing history

The ESP32 was first tested with the SIM800L cellular module before the rest of RoadLink was fully integrated. This reduced the number of variables involved and allowed the serial communication and data-transmission process to be developed separately.

The currently remembered test sequence is:

1. connect the ESP32 and SIM800L as an independent test setup;
2. use simulated RoadLink data instead of live vehicle data;
3. send the simulated information by SMS;
4. perform a later network-data test, believed to have used TCP/IP;
5. replace the SIM800L with the A7670SA for the later version of the project.

This sequence must be compared with the original sketches, photographs, messages, or test logs before it is marked as verified. The results should record what succeeded, what failed, and what changes were required—not only that a test was attempted.

## 6. Interfaces to document

| Connected subsystem | Probable interface | Information exchanged | Details still required |
| --- | --- | --- | --- |
| Cellular module | UART | AT commands, responses, and payload data | UART number, TX/RX pins, baud rate, control pins |
| GPS receiver | UART | Position, time, speed, and status data | Module model, UART number, pins, baud rate |
| CAN bus interface | SPI or integrated CAN/TWAI path | Vehicle frames and status | Controller/transceiver models and pins |
| TFT display | SPI or display-specific interface | Graphics, text, and status | Display model, pins, resolution, library |
| Rotary encoder | GPIO | Rotation and push-button events | Pins, pull-ups, debounce method |

“Probable interface” is used until each connection is verified against the actual RoadLink design.

## 7. Test evidence to add

The ESP32 section will be strongest if it includes repeatable evidence. Useful additions are:

- a clear photograph of the ESP32 and SIM800L test wiring;
- a clear photograph of the ESP32 and A7670SA test wiring;
- the exact simulated data packet or message;
- serial-monitor output showing commands and responses;
- the received SMS or server-side data;
- the firmware version used for each test;
- a table of test conditions, expected results, and actual results.

Suggested test table:

| Test | Configuration | Expected result | Actual result | Evidence | Status |
| --- | --- | --- | --- | --- | --- |
| SIM800L serial response | ESP32 + SIM800L | Module responds to an AT command | To be added | Serial output/photo | Not verified |
| SMS transmission | ESP32 + SIM800L + simulated data | Test phone receives the formatted data | To be added | SMS screenshot | Not verified |
| TCP/IP transmission | ESP32 + SIM800L + simulated data | Remote endpoint receives the payload | To be confirmed | Log/screenshot | Not verified |
| A7670SA communication | ESP32 + A7670SA | Module registers and exchanges data | To be added | Serial/network log | Not verified |

## 8. Design considerations and limitations

- UART traffic must be parsed without confusing normal responses, errors, and unsolicited messages from a modem.
- Cellular operations can take longer than local microcontroller operations, so timeouts and retry limits are necessary.
- The ESP32 and every peripheral must use compatible logic levels or suitable level shifting.
- The controller should remain responsive to the display and encoder while waiting for GPS, CAN, or cellular data.
- Module failures should produce useful diagnostic information instead of causing the entire firmware to stop.
- Pin assignments must account for pins with boot-time restrictions and for interfaces shared by multiple devices.

## 9. Information needed to complete this document

- ESP32 board/module name and revision;
- development framework and important libraries;
- current firmware architecture;
- final pin-assignment table;
- boot sequence and module initialization order;
- error-handling and recovery behavior;
- power consumption measurements;
- dated photographs and verified test results.

