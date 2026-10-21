# Hardware — SONiX Smart Connected Safety Gear

This document describes the hardware architecture, sensor interfaces, electrical requirements, MCU pin assignments, communication protocols, and prototype validation procedure for **SONiX — Smart Connected Safety Gear for Electrical and Welding Trades**.

The system is designed around an ESP32-C3 Mini microcontroller and integrates environmental, motion, ultraviolet, and non-contact temperature sensing. Sensor measurements are processed by the firmware to identify potentially unsafe working conditions and activate local warning indicators.

> **Prototype note:** The pin assignments and connections documented below represent a proposed integration architecture. Confirm the exact development-board variant, sensor breakout specifications, and physical wiring before using these assignments in firmware. The system is a prototype and is not certified personal protective equipment or a substitute for approved electrical and welding safety procedures.

---

## Target Hardware

| Item | Specification |
|---|---|
| Project | SONiX Smart Connected Safety Gear |
| Main controller | ESP32-C3 Mini development board |
| Processor | ESP32-C3, RISC-V architecture |
| Logic voltage | 3.3 V |
| Sensor interfaces | I²C, ADC, digital GPIO |
| Wireless connectivity | Wi-Fi and Bluetooth LE, subject to board configuration |
| Primary applications | Electrical maintenance and welding safety |
| Local alerts | Status LEDs and buzzer |
| Firmware environment | Arduino IDE or ESP-IDF, depending on implementation |

---

## Bill of Materials

| Component | Qty. | Function |
|---|---:|---|
| ESP32-C3 Mini | 1 | Main controller and sensor-data processing |
| VEML6075 UV sensor module | 1 | UV-A and UV-B measurement |
| MPU6050 IMU module | 1 | Acceleration, angular velocity, and fall-event analysis |
| MLX90614 temperature sensor module | 1 | Non-contact infrared temperature measurement |
| MQ-7 gas sensor module | 1 | Carbon monoxide sensing, subject to calibration and operating conditions |
| MQ-135 gas sensor module | 1 | Broad air-quality response to selected gases |
| Red LED | 1 | Warning indicator |
| Blue LED | 1 | Secondary status indicator |
| Green LED | 1 | Normal-operation indicator |
| Yellow LED | 1 | Caution indicator |
| Current-limiting resistors | As required | LED current control |
| Active buzzer | 1 | Audible warning |
| Regulated power supply | 1 | Stable power for controller and sensor modules |
| Jumper wires and connectors | As required | Electrical interconnections |
| Enclosure and mounting hardware | As required | Mechanical integration with the safety gear |

**Component selection note:** The MQ-7 and MQ-135 do not provide laboratory-grade gas identification by themselves. Their readings depend on warm-up, calibration, temperature, humidity, and interfering gases. They must not be treated as certified occupational gas detectors.

---

## System Architecture

```text
                  SONiX SMART SAFETY GEAR
                  ───────────────────────

 ┌────────────────────────────────────────────────────┐
 │                 SENSOR INPUTS                      │
 │                                                    │
 │  VEML6075       MPU6050       MLX90614              │
 │  UV sensing     Motion        IR temperature       │
 │      │              │               │              │
 │      └──────────────┼───────────────┘              │
 │                     │                              │
 │                 I²C BUS                            │
 │              SDA + SCL                             │
 │                     │                              │
 │  MQ-7 ── ADC ───────┤                              │
 │  MQ-135 ─ ADC ──────┤                              │
 │                     ▼                              │
 │            ┌─────────────────┐                     │
 │            │   ESP32-C3 Mini │                     │
 │            │                 │                     │
 │            │ Sensor polling  │                     │
 │            │ Data validation │                     │
 │            │ Threshold logic │                     │
 │            │ Event handling  │                     │
 │            └────────┬────────┘                     │
 │                     │                              │
 │           ┌─────────┴─────────┐                    │
 │           ▼                   ▼                    │
 │     Status LEDs          Active buzzer             │
 │                                                    │
 │          Optional Wi-Fi telemetry                  │
 └────────────────────────────────────────────────────┘
```

The architecture separates sensor acquisition from decision logic and warning outputs. Each sensor channel should be validated before its data is used for hazard classification.

---

## Pin Assignment

The following table is a **proposed example pin map**, not a verified record of the physical prototype.

| ESP32-C3 GPIO | Interface | Connected component | Function |
|---|---|---|---|
| GPIO4 | I²C SDA | VEML6075, MPU6050, MLX90614 | Shared sensor data line |
| GPIO5 | I²C SCL | VEML6075, MPU6050, MLX90614 | Shared sensor clock |
| GPIO0 | ADC input | MQ-7 analog output | Conditioned gas-sensor signal |
| GPIO1 | ADC input | MQ-135 analog output | Conditioned gas-sensor signal |
| GPIO3 | Digital output | Red LED | Warning indication |
| GPIO6 | Digital output | Blue LED | Secondary status indication |
| GPIO7 | Digital output | Green LED | Normal-operation indication |
| GPIO10 | Digital output | Yellow LED | Caution indication |
| GPIO2 | Digital output | Buzzer driver | Audible alert control |
| GND | Ground | All compatible modules | Common signal reference |
| 3V3 / 5V | Power | As specified by module | Sensor and controller supply |

> **Pin-map validation:** ESP32-C3 boards can differ in exposed pins, reserved connections, boot behaviour, and ADC availability. Check the exact board pinout and the selected board's firmware configuration before assigning these GPIOs. Do not assume every pin in this table is available on every Mini board.

---

## Sensor Interface and Electrical Requirements

### 1. VEML6075 — Ultraviolet Sensing

| Parameter | Requirement |
|---|---|
| Interface | I²C |
| Signals | SDA, SCL |
| Supply | Follow the selected breakout-board specification |
| Data | UV-A and UV-B measurements and derived UV index |
| Integration | Shared I²C bus |

The VEML6075 measures ultraviolet radiation in the UV-A and UV-B bands. The firmware can sample the sensor periodically and compare the resulting readings against configured thresholds.

The sensor is not a complete welding-eye protection system. UV exposure depends on sensor orientation, distance, shielding, reflections, and the welding process. Any alert thresholds require validation for the intended application.

### 2. MPU6050 — Motion and Fall-Event Analysis

| Parameter | Requirement |
|---|---|
| Interface | I²C |
| Signals | SDA, SCL |
| Supply | Depends on the bare IC or breakout module |
| Measurements | Three-axis acceleration and three-axis angular velocity |
| Application | Motion classification and potential fall-event detection |

The MPU6050 provides acceleration and angular-velocity measurements. Firmware can evaluate acceleration magnitude, changes in orientation, and inactivity to identify candidate fall events.

A single acceleration threshold is insufficient to reliably distinguish a fall from ordinary movements. Detection logic should be tested with representative movements and should not depend on hazardous real-world fall tests.

### 3. MLX90614 — Non-Contact Temperature Measurement

| Parameter | Requirement |
|---|---|
| Interface | SMBus-compatible two-wire interface |
| Signals | SDA, SCL |
| Supply | Verify the selected sensor module's rating |
| Measurement | Infrared object temperature; some variants also provide ambient temperature |
| Application | Non-contact temperature monitoring |

The MLX90614 measures infrared radiation emitted by a target. Its readings depend on the target's emissivity, field of view, distance, and surrounding thermal conditions.

The sensor should be positioned so that its field of view covers the intended target. It does not directly measure a worker's internal body temperature.

### 4. MQ-7 — Carbon Monoxide Sensing

| Parameter | Requirement |
|---|---|
| Interface | Analog output, if provided by the module |
| Supply | Common modules use a 5 V supply and heater circuit; verify the exact model |
| MCU input | ADC through a suitable voltage-conditioning stage |
| Application | Indicative carbon monoxide monitoring |

MQ-7 modules require a specified heater operating cycle and calibration procedure. Raw ADC values are not equivalent to carbon monoxide concentration in ppm.

**Electrical constraint:** Never connect a module's analog output directly to an ESP32-C3 input unless the maximum output is verified to remain within the MCU's permitted input range. Use a properly designed divider or buffer when necessary.

### 5. MQ-135 — Air-Quality Monitoring

| Parameter | Requirement |
|---|---|
| Interface | Analog output, if provided by the module |
| Supply | Common modules use 5 V; verify the module specification |
| MCU input | ADC through a suitable voltage-conditioning stage |
| Application | Broad response to selected air contaminants |

The MQ-135 responds to multiple gases and environmental conditions. It cannot independently identify a specific welding contaminant or provide reliable ppm readings without appropriate calibration and validation.

For occupational safety, use appropriately rated and calibrated detectors when a specific hazardous gas must be identified.

---

## MCU Peripheral Configuration

The following settings are proposed starting points for firmware development. They must be checked against the selected modules and board configuration.

### I²C — Digital Sensor Bus

| Setting | Proposed configuration |
|---|---|
| SDA | GPIO4 |
| SCL | GPIO5 |
| Initial bus speed | 100 kHz |
| Bus topology | Shared bus |
| Addressing | 7-bit device addressing |
| Pull-ups | Compatible with 3.3 V logic |
| Devices | VEML6075, MPU6050, MLX90614 |

All devices on the same I²C bus share SDA and SCL. Each device must have a unique address on that bus, and all connected devices must be electrically compatible.

Typical addresses commonly encountered are:

| Sensor | Typical 7-bit address |
|---|---|
| VEML6075 | `0x10` |
| MPU6050 | `0x68` or `0x69`, depending on AD0 |
| MLX90614 | `0x5A` |

Confirm addresses from the module documentation and with an I²C scanner. Breakout-board implementations and configuration options may differ.

### ADC — Analog Gas-Sensor Inputs

| Setting | Requirement |
|---|---|
| Inputs | Proposed GPIO0 and GPIO1 |
| Source | MQ-7 and MQ-135 analog outputs |
| Conversion | ADC counts to voltage |
| Conditioning | Required whenever the source can exceed the permitted input range |
| Calibration | Required before estimating gas concentration |

The ADC reports a digital representation of the input voltage. Converting ADC counts into a meaningful gas concentration requires a calibrated sensor model and consideration of heater conditions, sensor resistance, load resistance, and environmental factors.

### GPIO — Warning Outputs

| Output | Proposed GPIO | Purpose |
|---|---|---|
| Red LED | GPIO3 | Hazard warning |
| Blue LED | GPIO6 | Secondary status |
| Green LED | GPIO7 | Normal status |
| Yellow LED | GPIO10 | Caution |
| Buzzer driver | GPIO2 | Audible alert |

Use a current-limiting resistor for each LED. If the buzzer's current requirement exceeds the GPIO's permitted output current, drive it through a suitable transistor or MOSFET stage. Confirm whether the buzzer is active-high or active-low before finalizing the firmware.

---

## I²C Protocol Overview

I²C is a synchronous serial communication protocol using two primary signals:

- **SDA — Serial Data:** carries address, control, and sensor data.
- **SCL — Serial Clock:** synchronizes communication.

A typical sensor read transaction follows this sequence:

```text
START
  │
  ▼
7-bit device address + Write
  │
  ▼
ACK
  │
  ▼
Register address
  │
  ▼
ACK
  │
  ▼
Repeated START
  │
  ▼
7-bit device address + Read
  │
  ▼
ACK
  │
  ▼
Sensor data byte(s)
  │
  ▼
ACK / NACK
  │
  ▼
STOP
```

The exact transaction depends on the sensor's register map and driver implementation. Some sensors require additional configuration or measurement-conversion time before data can be read.

**Electrical requirements:**

1. SDA and SCL require suitable pull-up resistors.
2. Pull-up voltage must be compatible with the ESP32-C3's 3.3 V logic.
3. Avoid connecting a 5 V pull-up directly to an ESP32-C3 GPIO.
4. Verify that all I²C devices use compatible voltage levels.
5. Keep wiring short and provide a common ground reference.
6. Confirm the actual I²C addresses before running the application.

---

## Firmware Data-Processing Pipeline

```text
System Initialization
        │
        ▼
Configure GPIO, ADC and I²C
        │
        ▼
Initialize Sensor Drivers
        │
        ▼
Read Sensor Measurements
        │
        ▼
Validate Data and Sensor Status
        │
        ▼
Apply Calibration / Filtering
        │
        ▼
Evaluate Configured Thresholds
        │
        ▼
Classify Operating Condition
        │
        ├── Normal ─────► Normal status indication
        │
        ├── Caution ────► Caution indication
        │
        └── Hazard ─────► Warning LED + Audible Alert
                               │
                               ▼
                       Optional Event Logging
```

The thresholds, sampling intervals, sensor-fault handling, and alert-latching behaviour must be defined and validated in the firmware. Missing, stale, or invalid measurements should be handled explicitly rather than automatically classified as safe.

---

## Power and Grounding

The ESP32-C3 operates with 3.3 V logic. Sensor modules may have different supply requirements, so the power architecture must distinguish between the controller supply and any 5 V sensor or heater supply.

| Circuit | Design requirement |
|---|---|
| ESP32-C3 | Regulated supply appropriate to the board input |
| I²C logic | Pull-ups compatible with 3.3 V |
| MQ sensor heaters | Supply meeting the selected module's voltage and current requirements |
| Analog outputs | Conditioned to the permitted ADC range |
| LEDs | Individual current-limiting resistors |
| Buzzer | Driver stage if required by load current |
| Ground | Common reference for compatible signal circuits |

The supply must handle the ESP32-C3's wireless current peaks and the MQ sensor heater loads without excessive voltage drop. Use appropriate decoupling and verify the regulator's current capacity.

---

## Prototype Bring-Up and Validation

1. **Inspect the board:** confirm the exact ESP32-C3 Mini variant and its exposed GPIO pins.
2. **Verify power rails:** check the regulated voltage and ground connections before installing all modules.
3. **Test I²C separately:** connect one sensor at a time and verify its address and basic readings.
4. **Check analog levels:** measure each MQ module's analog output under its specified operating conditions before connecting it to the MCU.
5. **Validate individual sensors:** record baseline readings, noise, warm-up behaviour, and response to controlled test conditions.
6. **Test warning outputs:** verify each LED and buzzer independently using current-limited circuits.
7. **Integrate the firmware:** enable sensor acquisition, validation, filtering, and alert logic incrementally.
8. **Record test results:** document the sensor model, supply voltage, calibration settings, environmental conditions, and observed response.

---

## Design Limitations and Safety Considerations

- The listed sensor set does not, by itself, implement validated live-circuit proximity or electrical shock-current detection.
- MQ sensor readings must not be presented as calibrated gas concentrations without appropriate calibration and validation.
- UV and temperature sensors require suitable placement and testing for the intended exposure geometry.
- A fall-detection algorithm needs controlled validation and should account for false positives and missed events.
- The prototype must not be used as the sole means of protection against electrical shock, welding radiation, toxic gases, or other occupational hazards.

Before describing any feature as safety-rated, validate its detection performance, failure behaviour, electrical isolation where applicable, and suitability for the intended operating environment.

---

## Recommended Repository Structure

```text
SONiX/
├── README.md
├── CONNECTIONS.md
├── firmware/
│   └── SONiX_ESP32C3.ino
├── diagrams/
│   ├── sonix_system_architecture.png
│   └── sonix_connection_diagram.png
└── docs/
    └── sensor_calibration.md
```

To display the connection diagram in this document, add the image to the `diagrams/` directory and insert:

```markdown
![SONiX Hardware Connection Diagram](diagrams/sonix_connection_diagram.png)
```


