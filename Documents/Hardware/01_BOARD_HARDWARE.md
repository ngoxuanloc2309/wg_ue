
# Gateway V2 - Board Hardware Specification

**Document ID:** HW-001  
**Version:** 1.0  
**Status:** Draft

---

# 1. Purpose

This document describes the physical hardware used by the Gateway V2 platform.

It is intended to provide a single hardware overview for firmware development without requiring engineers or AI assistants to inspect the original schematic.

---

# 2. Product Summary

**Product Name**

Gateway V2

**Product Type**

Industrial IoT Gateway

**Main Functions**

- Industrial Modbus Gateway
- MQTT Edge Gateway
- Remote Monitoring
- Ethernet / Wi-Fi / LTE Connectivity
- OTA Firmware Update
- Local Web Configuration

---

# 3. Hardware Architecture

```text
                     +---------------------------+
                     |      STM32H563VGT6        |
                     |      Cortex-M33 MCU       |
                     +-------------+-------------+
                                   |
    --------------------------------------------------------------------
    |             |             |             |            |            |
 Ethernet      Wi-Fi/BLE       LTE        RS485 x3       USB        Storage
    |             |             |             |            |            |
 DP83848      ST67W611       A7680C      MAX485 x3     USB FS   SPI + OSPI
```

The STM32H563 is the only master controller. Every peripheral is managed by firmware running on the STM32.

---

# 4. Main Components

| Category | Device | Purpose |
|-----------|--------|---------|
| MCU | STM32H563VGT6 | Main Controller |
| Ethernet PHY | DP83848 | 10/100 Mbps Ethernet |
| Wi-Fi / BLE | ST67W611 | Wireless Connectivity |
| LTE Modem | A7680C | Cellular Connectivity |
| RS485 | MAX485 x3 | Modbus RTU |
| SPI Flash | W25Q128 | Configuration Storage |
| OctoSPI Flash | MX25LM51245 | OTA / Web Resources |
| USB | USB Type-C | Device / Debug |

---

# 5. Communication Interfaces

## Ethernet

- PHY: DP83848
- Interface: RMII
- Firmware Stack: HAL ETH + lwIP

## Wi-Fi / BLE

- Module: ST67W611
- Interface: SPI
- Used for:
  - Wi-Fi STA
  - Wi-Fi AP
  - BLE Provisioning

## LTE

- Module: A7680C
- Interface: UART
- Transport: PPPoS

## RS485

- Three independent channels
- MAX485 transceivers
- Modbus RTU Master / Slave

## USB

- USB Full-Speed Device
- Firmware update
- Virtual COM
- Debug

---

# 6. External Memory

## Internal Flash

- Bootloader
- Application

## SPI Flash (W25Q128)

- Device configuration
- Certificates
- User settings

## OctoSPI Flash (MX25LM51245)

- OTA image
- Web resources
- Data logging

---

# 7. Power System

Input Voltage

- 24V DC

Generated Rails

- +5V
- +3.3V

All digital peripherals operate at 3.3V logic.

---

# 8. Clock Sources

| Clock | Purpose |
|--------|---------|
| 16 MHz Crystal | MCU System Clock |
| 32.768 kHz Crystal | RTC |
| 50 MHz Oscillator | Ethernet RMII Clock |

---

# 9. Firmware Responsibilities

The firmware is expected to implement:

- BSP
- Driver Layer
- Network Manager
- Storage Manager
- Configuration Manager
- Event Bus
- Modbus Manager
- MQTT Service
- HTTP Server
- OTA Service
- Logger
- Watchdog

---

# 10. Hardware Design Principles

- STM32H563 is the only system master.
- Peripheral drivers must be independent.
- Application code must never directly access hardware.
- All network interfaces shall be controlled by Network Manager.
- Persistent data shall only be accessed through Storage Manager.
- Configuration shall only be modified through Config Manager.

---

# 11. Future Expansion

The board architecture allows future support for:

- Modbus TCP
- BACnet/IP
- OPC-UA Gateway
- Ethernet Failover
- Dual OTA Image
- Secure Boot
- TLS Certificate Management

---

# 12. AI Summary

**Board:** Gateway V2

**MCU:** STM32H563VGT6

**Networking**

- Ethernet (DP83848)
- Wi-Fi/BLE (ST67W611)
- LTE (A7680C)

**Industrial Interface**

- RS485 x3 (MAX485)

**Storage**

- Internal Flash
- W25Q128
- MX25LM51245

**Power**

- 24V Input
- 5V
- 3.3V

**Recommended Firmware Layers**

Application

↓

Services

↓

Managers

↓

Drivers

↓

HAL

This document provides the board-level hardware overview. Detailed pin assignments, interface specifications and bring-up procedures are documented separately.
