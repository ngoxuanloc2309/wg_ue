# Gateway V2 Hardware Context

**Version:** 1.0  
**Target Board:** Gateway V2 Industrial IoT Gateway

> This document is the primary hardware reference for firmware developers and AI assistants. Read this document before modifying firmware.

---

# 1. Product Overview

Gateway V2 is an Industrial IoT Gateway based on **STM32H563VGT6**.

The gateway bridges industrial field devices connected through RS485/Modbus RTU with Ethernet, Wi-Fi, LTE and Cloud services.

Primary applications:

- Industrial Automation
- Energy Monitoring
- Remote PLC Monitoring
- Edge Computing
- MQTT Gateway
- Modbus Gateway
- Remote OTA Update

---

# 2. System Architecture

```text
                          +-----------------------+
                          | STM32H563VGT6         |
                          | Cortex-M33            |
                          +-----------+-----------+
                                      |
    --------------------------------------------------------------------
    |            |            |             |             |             |
 Ethernet      WiFi/BLE      LTE         RS485 x3       USB        Storage
    |            |            |             |             |             |
 DP83848     ST67W611      A7680C       MAX485       USB FS     SPI / OSPI
```

STM32H563 is the **master controller** of the complete system.

---

# 3. Main MCU

| Item | Value |
|------|-------|
| MCU | STM32H563VGT6 |
| Core | ARM Cortex-M33 |
| Role | Main Controller |

Main responsibilities:

- System Control
- FreeRTOS
- Network Manager
- Storage Manager
- Modbus Manager
- MQTT
- HTTP
- OTA
- USB
- Ethernet
- Wi-Fi Management
- LTE Management

---

# 4. Communication Interfaces

## Ethernet

| Item | Value |
|------|------|
| PHY | DP83848 |
| Interface | RMII |
| Priority | Primary Network |

Firmware modules:

- eth_driver
- lwIP
- network_manager

Functions:

- DHCP
- Static IP
- MQTT
- HTTP
- HTTPS
- OTA

---

## Wi-Fi / BLE

| Item | Value |
|------|------|
| Module | ST67W611 |
| Interface | SPI |
| Features | Wi-Fi + BLE |

Firmware modules:

- st67_driver
- wifi_manager
- ble_manager
- network_manager

Functions:

- Wi-Fi STA
- Wi-Fi AP
- BLE Provisioning
- MQTT
- HTTP

---

## LTE

| Item | Value |
|------|------|
| Module | A7680C |
| Interface | UART |
| Transport | PPPoS |

Firmware modules:

- lte_driver
- pppos
- network_manager

Functions:

- Cellular Backup
- MQTT
- HTTP
- OTA

---

## RS485

| Item | Value |
|------|------|
| Ports | 3 |
| Transceiver | MAX485 |

Functions:

- Modbus RTU Master
- Modbus RTU Slave

Firmware module:

- modbus_manager

---

## USB

USB Full-Speed Device

Functions:

- Firmware Upgrade
- Virtual COM Port
- Debug

---

# 5. External Storage

## SPI Flash

Device:

- W25Q128

Purpose:

- Device Configuration
- Certificates
- User Settings

---

## OctoSPI Flash

Device:

- MX25LM51245

Purpose:

- OTA Images
- Web UI
- Data Logging
- Future Database

---

# 6. Power System

Input Voltage:

- 24V DC

Power Rails:

- +5V
- +3.3V

Assumption:

All logic peripherals operate at **3.3V**.

---

# 7. Clock Sources

| Source | Purpose |
|---------|---------|
| 16 MHz Crystal | Main MCU Clock |
| 32.768 kHz Crystal | RTC |
| 50 MHz Oscillator | Ethernet RMII Reference |

---

# 8. Network Priority

The firmware shall use the following priority:

1. Ethernet
2. Wi-Fi
3. LTE

Network selection shall only be performed by **Network Manager**.

Applications must **never** directly choose a network interface.

---

# 9. Storage Strategy

## Internal Flash

- Bootloader
- Main Firmware
- Factory Configuration

## SPI Flash

- Configuration
- Certificates
- Calibration

## OctoSPI Flash

- OTA Images
- Web Resources
- Data Logs

---

# 10. Expected Firmware Modules

Core:

- BSP
- Driver Manager
- Event Bus
- Config Manager
- Storage Manager
- Network Manager

Drivers:

- Ethernet Driver
- Wi-Fi Driver
- LTE Driver
- RS485 Driver
- USB Driver
- Flash Driver

Services:

- MQTT
- HTTP/HTTPS
- OTA
- NTP
- Web Configuration
- Logger
- Watchdog

---

# 11. Boot Flow

```text
Power On
    │
    ▼
Clock Initialization
    │
    ▼
GPIO Initialization
    │
    ▼
Storage Initialization
    │
    ▼
Configuration Loading
    │
    ▼
Driver Initialization
    │
    ▼
Network Manager
    │
    ▼
Application
```

---

# 12. AI Development Rules

When generating firmware:

- Treat STM32H563 as the only master controller.
- Do not allow application code to directly access hardware.
- All hardware access must go through BSP and Driver layers.
- All network access must go through Network Manager.
- All configuration access must go through Config Manager.
- All persistent storage must go through Storage Manager.
- Use Event Bus for communication between modules.
- Design firmware using layered architecture:

```text
Application
    ↓
Services
    ↓
Managers
    ↓
Drivers
    ↓
HAL
```

This document is the canonical hardware context for Gateway V2.
