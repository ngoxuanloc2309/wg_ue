# Gateway Firmware

> Industrial IoT Gateway Firmware supporting multiple network interfaces, industrial communication protocols, and secure cloud connectivity.

---

# Overview

This project implements an industrial IoT Gateway capable of bridging field devices connected through **RS485/Modbus RTU** with Ethernet, Wi-Fi, Cellular LTE, USB, and Cloud services.

The firmware is designed with a modular architecture that allows multiple network interfaces to operate simultaneously while providing secure communication with cloud platforms and local configuration through a web interface.

---

# Hardware

## Communication Interfaces

| Peripheral | Quantity | Description |
|------------|---------:|-------------|
| Ethernet | 1 | 10/100 Mbps |
| Wi-Fi | 1 | IEEE 802.11 b/g/n |
| Bluetooth LE | 1 | BLE 5.x |
| 4G LTE | 1 | PPP over Serial (PPPoS) |
| USB | 1 | USB Full Speed Device |
| RS485 | 3 | Independent Modbus RTU ports |

---

# Software Features

## Multi Network Interface

The gateway supports simultaneous operation of multiple network interfaces.

| Interface | Description |
|-----------|-------------|
| eth-netif | Ethernet Interface |
| wifi-netif | Wi-Fi Station/AP |
| pppos-netif | 4G LTE via PPPoS |
| usb-netif | USB Networking |

Features:

- Multiple interfaces active simultaneously
- Independent DHCP/Static IP configuration
- Automatic network failover
- Interface priority management
- Route selection
- Network monitoring

---

## Cloud Connectivity

Supported protocols

- MQTT
- MQTT over TLS (MQTTS)
- HTTP Client
- HTTPS Client
- NTP Client

Capabilities

- Secure TLS communication
- Certificate management
- Automatic reconnect
- Publish/Subscribe
- OTA ready
- JSON payload support

---

## Industrial Protocols

### Modbus RTU

Supported over all three RS485 ports.

Features

- Master mode
- Multiple slaves
- Configurable baudrate
- Configurable parity
- Configurable stop bits
- Poll scheduler
- Register mapping

### Modbus TCP

- TCP Client
- TCP Server
- Ethernet transport
- Wi-Fi transport

---

## Modbus Gateway

The firmware can bridge Modbus RTU devices to IP networks.

Supported gateway modes

- Modbus RTU ↔ Modbus TCP
- Modbus RTU → MQTT
- Modbus RTU → Ethernet
- Modbus RTU → Wi-Fi
- Modbus RTU → LTE

---

## BLE Gateway

Bluetooth Low Energy gateway features

- BLE Scanner
- BLE Peripheral Discovery
- Advertisement Parser
- GATT Client
- Sensor Data Collection
- BLE to MQTT Bridge

---

## Web Configuration

Embedded HTTP/HTTPS Server provides device configuration.

Configurable parameters include

- Ethernet
- Wi-Fi
- LTE
- MQTT Broker
- HTTP Server
- Time Server
- Modbus Configuration
- RS485 Parameters
- User Management
- Firmware Upgrade
- Certificates
- Device Information

---

## Time Synchronization

Supported

- NTP Client
- Automatic timezone support
- RTC synchronization

---

## Security

- HTTPS
- MQTTS
- TLS 1.2+
- Certificate Authentication
- Secure Credential Storage
- Password Authentication

---

# Network Architecture

```text
                         +----------------------+
                         |     MQTT Broker      |
                         +----------+-----------+
                                    |
                     Ethernet / WiFi / LTE
                                    |
                    +---------------+---------------+
                    |                               |
                    |        Gateway Firmware       |
                    |                               |
                    +---------------+---------------+
                                    |
          -------------------------------------------------------
          |                  |                  |
      RS485-1            RS485-2            RS485-3
          |                  |                  |
     Modbus RTU         Modbus RTU         Modbus RTU
          |                  |                  |
      PLC / Meter       Sensor / IO       Inverter
```

---

# Software Stack

```text
+--------------------------------------------------------+
|                    User Applications                   |
+--------------------------------------------------------+
| MQTT | HTTP | Modbus | BLE | Configuration | Gateway   |
+--------------------------------------------------------+
|        Network Services / Middleware                   |
| HTTP | HTTPS | MQTT | TLS | NTP | Modbus TCP | BLE     |
+--------------------------------------------------------+
|                 Multi Network Manager                  |
| Ethernet | WiFi | PPPoS | USB                          |
+--------------------------------------------------------+
|               Drivers / BSP / HAL                      |
| ETH | UART | USB | SPI | GPIO | DMA | Timer | RTC      |
+--------------------------------------------------------+
|                    RTOS / MCU                          |
+--------------------------------------------------------+
```

---

# Supported Gateway Functions

| Source | Destination |
|---------|-------------|
| Modbus RTU | MQTT |
| Modbus RTU | MQTTS |
| Modbus RTU | Modbus TCP |
| Modbus RTU | Ethernet |
| Modbus RTU | Wi-Fi |
| Modbus RTU | LTE |
| BLE | MQTT |
| BLE | HTTP |
| BLE | Ethernet |

---

# Typical Applications

- Industrial IoT Gateway
- Factory Data Collection
- PLC Remote Monitoring
- Energy Meter Gateway
- Smart Building Gateway
- Environmental Monitoring
- Remote SCADA Communication
- BLE Sensor Gateway
- Edge Data Aggregation

---

# Project Structure

```text
gateway/
├── apps/
├── components/
├── middleware/
│   ├── mqtt/
│   ├── http/
│   ├── modbus/
│   ├── ble/
│   ├── lwip/
│   └── mbedtls/
├── network/
│   ├── eth_netif/
│   ├── wifi_netif/
│   ├── pppos_netif/
│   ├── usb_netif/
│   └── network_manager/
├── drivers/
├── bsp/
├── configs/
├── docs/
└── tests/
```

---

# Future Features

- OTA Update
- VPN Support (WireGuard/OpenVPN)
- SNMP
- OPC UA
- BACnet/IP
- Ethernet/IP
- PROFINET Gateway
- Data Logging
- Local Database
- Edge Computing
- REST API
- WebSocket
- Docker-based Linux Simulator

---

# Getting Started

## Prerequisites

- **CMake** (>= 3.20)
- **MinGW / Make** (for Windows build environment)
- **GCC ARM Toolchain** (`arm-none-eabi-gcc`)
- **Git** (for cloning and submodules)
- **STM32CubeProgrammer** (for flashing)

## Setup

Clone the repository:

```bash
git clone <repository_url>
cd GatewayV2
```

Initialize the Git submodules by running the `setup.bat` script:

```bat
setup.bat
```
*(Alternatively, you can manually run `git submodule update --init --recursive`)*

## Build

Run the `build.bat` script to configure CMake and compile the firmware:

```bat
build.bat
```

After a successful build, the output files (`GatewayV2.elf`, `GatewayV2.hex`, `GatewayV2.bin`) will be generated in the `build/` directory.

## Flashing

You can flash the compiled firmware to your STM32H563 board by running the `flash.bat` script (requires STM32CubeProgrammer installed):

```bat
flash.bat
```

The current bring-up configuration runs the application directly without the bootloader:

- Application vector table: `0x08000000`
- Application image: `build/GatewayV2.bin`
- Flash address: `0x08000000`
- Flash command: `flash.bat`

The bootloader remains a separate target. Use `bootloader/flash.bat` only when testing the bootloader flow.

## Runtime and diagnostics

- Logging uses the `libs/logger` submodule with the existing `LOGD`, `LOGI`, `LOGW`, and `LOGE` APIs.
- Logger output uses a direct callback, bounded formatting, and a finite FreeRTOS mutex timeout.
- The logger banner is disabled by default for product builds.
- USB networking is disabled by default during STM32H563 USB DRD FS bring-up (`GW_USB_NETIF_ENABLE=0`).
- System monitoring reports heap, minimum heap, CPU, and task stack usage every 30 seconds.
- The serial shell runs on USART1/LPUART1 at 115200 baud.

---

# License

This project is licensed under the MIT License.
