# Cellular communication subsystem

**Document status:** Initial draft  
**Subsystem versions:** SIM800L (early prototype) and A7670SA (later version)

## 1. Purpose

The cellular subsystem allows RoadLink to communicate beyond the vehicle. The ESP32 prepares the information to be sent, while the cellular module handles communication with the mobile network. Keeping these responsibilities separate means that the controller can focus on RoadLink's logic and the modem can focus on network registration and transmission.

During development, cellular communication was tested independently with simulated data. This was important because it allowed the transmission method to be evaluated before CAN bus, GPS, display, and user-interface integration introduced additional variables.

## 2. Development evolution

RoadLink has used two cellular-module stages:

1. **SIM800L prototype.** The first version used an ESP32 connected to a SIM800L. Simulated RoadLink data was initially transmitted by SMS. A later test is believed to have moved from SMS to a TCP/IP data connection.
2. **A7670SA version.** The project later changed to an A7670SA module. This became the cellular platform for the newer RoadLink design.

The exact dates, test outcomes, and engineering reason for the change still need to be recovered from the original code, photographs, and test records. Until that evidence is added, the sequence above should be treated as a preliminary project history.

## 3. First stage: SIM800L

### 3.1 Role in the first prototype

The SIM800L provided a practical way to prove that the ESP32 could control a cellular modem using serial commands. The test setup was intentionally limited to the ESP32 and cellular module, with simulated values standing in for information that would later come from RoadLink's other subsystems.

This approach tested several essential ideas:

- serial communication between the ESP32 and modem;
- modem control through AT commands;
- SIM and mobile-network access;
- conversion of internal values into a transmittable message;
- confirmation that information reached an external destination.

### 3.2 SMS test

In the first transmission method, the ESP32 formatted simulated data as a text message and instructed the SIM800L to send it by SMS. SMS was useful for an early proof of concept because the received message itself provided visible confirmation of end-to-end transmission.

The final record of this test should include:

- the exact simulated values and message format;
- the essential command/response sequence;
- whether the message arrived correctly and how long it took;
- any registration, signal, antenna, SIM, or power problems;
- a screenshot or photograph of the received SMS;
- the firmware used for the test.

### 3.3 TCP/IP test

After the SMS stage, development is believed to have progressed to a TCP/IP-based transmission test. Unlike SMS, this method can send application data through a packet-data connection to a remote service. It is closer to the communication method normally required for continuous or structured telemetry.

This part of the history needs confirmation. The documentation should identify:

- whether TCP or UDP was used;
- the access point name (APN) configuration, excluding private credentials;
- the destination type, such as a test server or cloud endpoint;
- the payload format;
- how a successful connection and delivery were verified;
- the behavior after a timeout, disconnect, or modem reset.

No server address, SIM identifier, telephone number, password, or private API key should be committed to the public repository.

## 4. Second stage: A7670SA

The A7670SA replaced the SIM800L in the later RoadLink design. Although both modules are controlled through a serial AT-command interface, they should not be treated as interchangeable. Commands, network capabilities, power requirements, status indications, initialization, and library support can differ.

The migration therefore requires verification in several areas:

- ESP32 UART settings and control pins;
- module power-on and reset sequence;
- SIM detection and network registration;
- antenna connection and signal-quality checks;
- packet-data context and network configuration;
- data-session commands and response parsing;
- recovery after loss of network or power instability.

The specific motivation for selecting the A7670SA should be documented using the project's actual decision. Possible considerations must not be presented as facts until confirmed.

## 5. Communication with the ESP32

At a logical level, both cellular versions follow the same exchange:

```text
┌────────────────────────┐                       ┌─────────────────────────┐
│         ESP32          │                       │     Cellular module     │
│                        │  AT command / data    │   SIM800L or A7670SA    │
│ Format RoadLink data   │──────────────────────►│                         │
│ Parse modem responses  │◄──────────────────────│ Network/status response │
│ Apply timeout/retry    │   UART serial link    │ Send through network    │
└────────────────────────┘                       └────────────┬────────────┘
                                                             │
                                                             ▼
                                                   Mobile network / remote
                                                        destination
```

The final wiring diagram must show at least TX, RX, ground, module power, and any power-key, reset, or status pins actually used. It must also distinguish the modem's main supply from its UART logic level.

## 6. Power-supply importance

Cellular transmitters can create rapid changes in current demand. A supply that appears correct when measured without a transmission may still dip or become noisy when the modem connects to the network or transmits. Symptoms can include random resets, failed registration, incomplete messages, or serial responses that stop unexpectedly.

For that reason, cellular testing should record:

- module supply voltage at idle and during transmission;
- supply capacity and regulator used;
- grounding arrangement;
- local decoupling or bulk capacitance;
- reset behavior during network activity.

Exact acceptable voltage and current values must be taken from the datasheet for the precise module board used, not assumed from the modem family name. The full power design will be covered in the PCB and power-supply document.

## 7. Comparative record

This table separates confirmed project history from details that still need evidence.

| Topic | SIM800L prototype | A7670SA version |
| --- | --- | --- |
| Position in project | First cellular prototype | Later/current cellular choice |
| Controller | ESP32 | ESP32 |
| Early data source | Simulated RoadLink data | To be documented |
| Demonstrated method | SMS | To be documented |
| Later data method | TCP/IP believed to have been tested | To be documented |
| Physical interface | UART expected; verify wiring | UART expected; verify wiring |
| Test evidence | Photos, code, SMS, and logs needed | Photos, code, and logs needed |
| Migration reason | Not yet documented | Not yet documented |

## 8. Proposed repeatable test procedure

The following procedure can be adapted for both modules:

1. inspect all wiring and confirm the required supply and logic levels;
2. power the modem from the intended supply while monitoring its voltage;
3. open the ESP32 serial log and confirm basic AT-command communication;
4. verify SIM detection;
5. wait for network registration and record the result;
6. read and record signal quality;
7. configure the selected SMS or packet-data service;
8. transmit a known test payload with a timestamp or sequence number;
9. verify receipt at the destination;
10. repeat the test and record failures, delays, resets, and recovery behavior.

| Field | Value to record |
| --- | --- |
| Date and firmware version | To be added |
| Cellular module and board revision | To be added |
| SIM/carrier | To be added without private identifiers |
| Antenna | To be added |
| Supply and measured voltage | To be added |
| Signal-quality result | To be added |
| Payload | To be added |
| Expected result | To be added |
| Actual result | To be added |
| Delivery time | To be added |
| Pass/fail and observations | To be added |

## 9. Images and evidence to add

When the photographs are available, add them to `documentation/assets/images/` and place them near the relevant test description. Useful images include:

- ESP32 with SIM800L only, showing the test wiring;
- the received SMS containing simulated data;
- evidence of the SIM800L packet-data test;
- ESP32 with A7670SA only, showing the later test wiring;
- serial output for registration, signal quality, and transmission;
- a labeled wiring diagram derived from the photographs and verified against the circuit.

Each photograph should have a caption stating the module, development stage, and purpose of the setup. Any visible telephone numbers, SIM numbers, credentials, or private endpoints should be removed before publication.

## 10. Information needed to complete this document

- exact SIM800L and A7670SA breakout-board versions;
- dated order of the tests;
- confirmed result of the SMS test;
- confirmation of the TCP/IP test and its protocol;
- sample payloads with private data removed;
- final A7670SA transmission method and test result;
- UART baud rates, ESP32 pins, and modem control pins;
- antennas, carrier/network, and relevant configuration;
- measured supply voltage and behavior during transmission;
- actual reason for changing modules;
- photographs, firmware, and serial logs.

