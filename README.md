# ESP32-Based Wireless Embedded Control System for Dual-Motor Robotic Vehicle

An embedded systems engineering project focused on real-time wireless vehicle control using dual ESP32 microcontrollers communicating through low-latency UDP-based Wi-Fi networking.

---

## Project Overview

This project implements a wireless robotic vehicle platform composed of two independent embedded subsystems:

### ESP32-S3 Remote Controller
Responsible for:
- joystick input processing
- button handling
- wireless UDP packet transmission
- real-time user interaction

### ESP32 Vehicle Controller
Responsible for:
- UDP packet reception
- motor control through L293D
- PWM speed regulation
- LED and buzzer control
- real-time command interpretation

The system operates through continuous low-latency Wi-Fi communication to provide responsive wireless vehicle control.

---

## Key Features

- Real-time wireless vehicle control
- Dual ESP32 architecture
- UDP-based communication system
- PWM motor speed control
- Independent steering and drive control
- LED and buzzer peripheral integration
- Power-domain separation
- Capacitor-assisted voltage stabilization
- Embedded systems debugging and analysis

---

## System Architecture

### Hardware Architecture
The platform is divided into:
- remote controller subsystem
- wireless communication subsystem
- onboard vehicle control subsystem
- motor driver subsystem
- peripheral output subsystem

### Software Architecture

1. Controller reads joystick/button inputs  
2. Commands are converted into UDP packets  
3. Wi-Fi transmits packets to vehicle ESP32  
4. Vehicle ESP32 parses incoming commands  
5. GPIO outputs control motors and peripherals  

---

## Hardware Components

| Component | Function |
|---|---|
| ESP32 | Vehicle control processor |
| ESP32-S3 | Wireless remote controller |
| L293D | Dual H-bridge motor driver |
| DC Motors | Drive + steering actuation |
| LEDs | Status indication |
| Buzzer | Audible feedback |
| Capacitors | Voltage stabilization |
| Breadboard | Prototype platform |
| 3× AA Battery Pack | Motor power supply |
| 5V Power Module | Logic power rail |

---

## Communication Protocol

The system uses:
- Wi-Fi Access Point architecture
- UDP packet transmission
- low-latency real-time communication

UDP was selected to prioritize responsiveness and continuous control performance over guaranteed packet acknowledgment.

---

## Power Architecture

The system uses separated power domains:

| Subsystem | Power Source |
|---|---|
| ESP32 Logic Rail | 5V regulated supply |
| L293D Logic Supply (VCC1) | 5V |
| Motor Power Supply (VCC2) | 4.5V AA battery pack |
| ESP32-S3 Remote | USB-C power |

This separation reduced:
- voltage dips
- ESP32 resets
- Wi-Fi instability
- motor-induced electrical noise

---

## Engineering Challenges

During development, multiple system-level issues were encountered:
- ESP32 brownout resets
- Wi-Fi disconnections
- motor noise interference
- unstable power delivery
- breadboard wiring instability

These were mitigated using:
- separated power domains
- decoupling capacitors
- revised motor power routing
- improved system architecture

---

## Electrical Engineering Concepts Demonstrated

- PWM motor control
- voltage drop analysis
- current distribution
- power-domain separation
- capacitor decoupling
- transient response behavior
- motor driver efficiency analysis
- embedded networking
- mixed-signal system integration

---

## Repository Structure

```text
/project-root
│
├── /code
│   ├── esp32_vehicle_controller.ino
│   └── esp32s3_remote_controller.ino
│
├── /schematics
│   ├── esp32_schematic.png
│   ├── l293d_schematic.png
│   ├── motor_schematic.png
│   └── led_buzzer_schematic.png
│
├── /block_diagrams
│   ├── hardware_block_diagram.png
│   └── software_block_diagram.png
│
├── /images
│   ├── prototype_photo_1.jpg
│   ├── prototype_photo_2.jpg
│   └── wiring_layout.jpg
│
├── /documentation
│   └── engineering_report.pdf
│
└── README.md
```

---

## Future Improvements

Planned future upgrades include:
- custom PCB implementation
- MOSFET-based motor drivers
- Li-ion battery integration
- encoder feedback systems
- telemetry support
- autonomous navigation features
- FPV camera integration
- closed-loop control algorithms

---

## Prototype Status

**Current Version:**  
Version 1.0 — Breadboard Prototype

**Planned Version:**  
Version 2.0 — PCB-Based Embedded Platform

---

## Development Tools

- Arduino IDE
- ESP32 Board Support Package
- KiCad
- GitHub
- Embedded C++

---

## Documentation

Full engineering report included:
- system architecture
- schematics
- calculations
- testing
- limitations
- future improvements
- embedded systems analysis

---

## Final Project Summary

This project demonstrates the development of a real-time wireless embedded control system integrating electrical engineering principles, embedded programming, wireless communication, and system-level debugging into a fully functional robotic vehicle platform.

The project emphasizes:
- engineering iteration
- practical debugging
- electrical analysis
- hardware/software integration
- real-world embedded systems design
