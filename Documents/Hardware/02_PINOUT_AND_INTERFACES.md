# Gateway V2 - Pinout and Interfaces Specification

**Document ID:** HW-002  
**Version:** 1.1  
**Status:** Draft  
**Source:** Gateway V2 schematic, `Ethernet Wifi Gateway.pdf`

---

# 1. Purpose

This document describes the **actual MCU pins connected on the Gateway V2 board**, including MCU package pins, STM32 GPIO names, schematic net names, peripheral usage, and firmware ownership.

This file is intended for:

- BSP development
- Driver development
- CubeMX configuration
- Firmware bring-up
- AI coding assistants

The goal is to let firmware development proceed without repeatedly opening the schematic.

---

# 2. Hardware Platform Summary

| Item | Value |
|------|-------|
| Main MCU | STM32H563VGT6 |
| Package | LQFP100 |
| Ethernet PHY | DP83848 |
| Wi-Fi / BLE Module | ST67W611 |
| LTE Module | A7680C |
| RS485 Transceiver | MAX485 x3 |
| SPI Flash | W25Q128 |
| OctoSPI Flash | MX25LM51245 |
| USB | USB Type-C, USB FS Device |

---

# 3. MCU Pin Assignment Table

## 3.1 Ethernet RMII - DP83848

| MCU Pin | STM32 GPIO | Schematic Net | Direction | Function | Firmware Owner |
|--------:|------------|---------------|-----------|----------|----------------|
| 24 | PA1 | ETH_REF_CLK | Input | RMII 50 MHz reference clock | eth_driver |
| 25 | PA2 | ETH_MDIO | Bidirectional | PHY management data | eth_driver |
| 16 | PC1 | ETH_MDC | Output | PHY management clock | eth_driver |
| 30 | PA5 | ETH_TX_EN | Output | RMII transmit enable | eth_driver |
| 51 | PB12 | ETH_TXD0 | Output | RMII transmit data 0 | eth_driver |
| 54 | PB15 | ETH_TXD1 | Output | RMII transmit data 1 | eth_driver |
| 32 | PA7 | ETH_CRS_DV | Input | RMII carrier sense / receive data valid | eth_driver |
| 33 | PC4 | ETH_RXD0 | Input | RMII receive data 0 | eth_driver |
| 34 | PC5 | ETH_RXD1 | Input | RMII receive data 1 | eth_driver |

---

## 3.2 Wi-Fi / BLE - ST67W611

| MCU Pin | STM32 GPIO | Schematic Net | Direction | Function | Firmware Owner |
|--------:|------------|---------------|-----------|----------|----------------|
| 88 | PD7 | ST67_NCS | Output | SPI chip select | st67_driver |
| 89 | PB3 | ST67_SCK | Output | SPI clock | st67_driver |
| 90 | PB4 | ST67_MISO | Input | SPI MISO | st67_driver |
| 91 | PB5 | ST67_MOSI | Output | SPI MOSI | st67_driver |
| 81 | PD0 | ST67_RDY | Input | Module ready / interrupt | st67_driver |
| 82 | PD1 | ST67_EN | Output | Module enable | st67_driver |
| 83 | PD2 | ST67_BOOT | Output | Boot mode select | st67_driver |
| 84 | PD3 | ST67_Wakeup | Output | Wakeup control | st67_driver |
| 85 | PD4 | ST67_32K | Clock / GPIO | 32 kHz reference / control | st67_driver |
| 86 | PD5 | ST67_RX | UART RX into module | Auxiliary UART | st67_driver |
| 87 | PD6 | ST67_TX | UART TX from module | Auxiliary UART | st67_driver |

---

## 3.3 LTE - A7680C

| MCU Pin | STM32 GPIO | Schematic Net | Direction | Function | Firmware Owner |
|--------:|------------|---------------|-----------|----------|----------------|
| 92 | PB6 | A7680C_RX | Input to MCU | LTE UART TX to host | lte_driver |
| 93 | PB7 | A7680C_TX | Output from MCU | LTE UART RX from host | lte_driver |
| 95 | PB8 | A7680C_PWR | Output | LTE PWRKEY control | lte_driver |
| 96 | PB9 | A7680C_RST | Output | LTE reset control | lte_driver |
| 70 | PA11 | USB_N | USB | USB D- shared board net | usb_driver / lte_driver |
| 71 | PA12 | USB_P | USB | USB D+ shared board net | usb_driver / lte_driver |

> Note: USB routing between board USB and LTE USB must be verified during bring-up.

---

## 3.4 RS485 / Modbus RTU

### RS485 Port 1

| MCU Pin | STM32 GPIO | Schematic Net | Direction | Function | Firmware Owner |
|--------:|------------|---------------|-----------|----------|----------------|
| 55 | PD8 | USART3_TX | Output | UART TX to MAX485 DI | rs485_driver |
| 56 | PD9 | USART3_RX | Input | UART RX from MAX485 RO | rs485_driver |
| 57 | PD10 | USART3_DE | Output | MAX485 DE/RE control | rs485_driver |

### RS485 Port 2

| MCU Pin | STM32 GPIO | Schematic Net | Direction | Function | Firmware Owner |
|--------:|------------|---------------|-----------|----------|----------------|
| 63 | PC6 | USART6_TX | Output | UART TX to MAX485 DI | rs485_driver |
| 64 | PC7 | USART6_RX | Input | UART RX from MAX485 RO | rs485_driver |
| 65 | PC8 | USART6_DE | Output | MAX485 DE/RE control | rs485_driver |

### RS485 Port 3

| MCU Pin | STM32 GPIO | Schematic Net | Direction | Function | Firmware Owner |
|--------:|------------|---------------|-----------|----------|----------------|
| 2 | PE3 | USART10_TX | Output | UART TX to MAX485 DI | rs485_driver |
| 3 | PE4 | USART10_RX | Input | UART RX from MAX485 RO | rs485_driver |
| 1 | PE2 | USART10_DE | Output | MAX485 DE/RE control | rs485_driver |

---

## 3.5 SPI Flash - W25Q128

| MCU Pin | STM32 GPIO | Schematic Net | Direction | Function | Firmware Owner |
|--------:|------------|---------------|-----------|----------|----------------|
| 77 | PA15 | SPI_NSS / SPI_CS | Output | SPI chip select | spi_flash_driver |
| 78 | PC10 | SPI_SCK / SPI_CLK | Output | SPI clock | spi_flash_driver |
| 79 | PC11 | SPI_MISO | Input | SPI MISO | spi_flash_driver |
| 80 | PC12 | SPI_MOSI | Output | SPI MOSI | spi_flash_driver |

---

## 3.6 OctoSPI Flash - MX25LM51245

| MCU Pin | STM32 GPIO | Schematic Net | Direction | Function | Firmware Owner |
|--------:|------------|---------------|-----------|----------|----------------|
| 26 | PA3 | OCTOSPI_CLK | Output | OctoSPI clock | ospi_flash_driver |
| 42 | PE11 | OCTOSPI_NCS | Output | OctoSPI chip select | ospi_flash_driver |
| 37 | PB2 | OCTOSPI_DQS | Bidirectional | Data strobe | ospi_flash_driver |
| 36 | PB1 | OCTOSPI_IO0 | Bidirectional | Data IO0 | ospi_flash_driver |
| 35 | PB0 | OCTOSPI_IO1 | Bidirectional | Data IO1 | ospi_flash_driver |
| 17 | PC2 | OCTOSPI_IO2 | Bidirectional | Data IO2 | ospi_flash_driver |
| 31 | PA6 | OCTOSPI_IO3 | Bidirectional | Data IO3 | ospi_flash_driver |
| 38 | PE7 | OCTOSPI_IO4 | Bidirectional | Data IO4 | ospi_flash_driver |
| 39 | PE8 | OCTOSPI_IO5 | Bidirectional | Data IO5 | ospi_flash_driver |
| 40 | PE9 | OCTOSPI_IO6 | Bidirectional | Data IO6 | ospi_flash_driver |
| 41 | PE10 | OCTOSPI_IO7 | Bidirectional | Data IO7 | ospi_flash_driver |

---

## 3.7 USB FS

| MCU Pin | STM32 GPIO | Schematic Net | Direction | Function | Firmware Owner |
|--------:|------------|---------------|-----------|----------|----------------|
| 70 | PA11 | USB_N | USB | USB FS D- | usb_driver |
| 71 | PA12 | USB_P | USB | USB FS D+ | usb_driver |
| 73 | VDDUSB | VDDUSB | Power | USB supply | bsp_power |

---

## 3.8 Debug Interface

| MCU Pin | STM32 GPIO | Schematic Net | Direction | Function | Firmware Owner |
|--------:|------------|---------------|-----------|----------|----------------|
| 72 | PA13 | SWDIO | Bidirectional | SWD data | debug |
| 76 | PA14 | SWCLK | Input | SWD clock | debug |
| 14 | NRST | NRST | Input | MCU reset | debug / bsp |
| 68 | PA9 | DEBUG_TX | Output | Debug UART TX | logger |
| 69 | PA10 | DEBUG_RX | Input | Debug UART RX | logger |

---

## 3.9 Boot, Buttons, LEDs, Expansion GPIO

| MCU Pin | STM32 GPIO | Schematic Net | Function / Notes |
|--------:|------------|---------------|------------------|
| 94 | BOOT0 | BOOT | MCU boot mode |
| 97 | PE0 | PE0 / LED | User LED / GPIO |
| 7 | PC13 | PC13 | User GPIO / button candidate |
| 4 | PE5 | PE5 | Expansion / GPIO |
| 5 | PE6 | PE6 | Expansion / GPIO |
| 23 | PA0 | PA0 | Expansion / GPIO |
| 29 | PA4 | PA4 | Expansion / GPIO |
| 67 | PA8 | PA8 | Expansion / GPIO |
| 47 | PB10 | PB10 | Expansion / GPIO |
| 52 | PB13 | PB13 | Expansion / GPIO |
| 53 | PB14 | PB14 | Expansion / GPIO |
| 15 | PC0 | PC0 | Expansion / GPIO |
| 18 | PC3 | PC3 | Expansion / GPIO |
| 66 | PC9 | PC9 | Expansion / GPIO |
| 43 | PE12 | PE12 | Expansion / GPIO |
| 44 | PE13 | PE13 | Expansion / GPIO |
| 45 | PE14 | PE14 | Expansion / GPIO |
| 46 | PE15 | PE15 | Expansion / GPIO |
| 58 | PD11 | PD11 | Expansion / GPIO |
| 59 | PD12 | PD12 | Expansion / GPIO |
| 60 | PD13 | PD13 | Expansion / GPIO |
| 61 | PD14 | PD14 | Expansion / GPIO |
| 62 | PD15 | PD15 | Expansion / GPIO |

---

# 4. Interface-Level Specification

## 4.1 Ethernet Interface

| Item | Value |
|------|-------|
| PHY | DP83848 |
| Interface | RMII |
| Connector | RJ45 HR911105A |
| Clock | 50 MHz oscillator |
| Driver | `eth_driver` |
| Manager | `network_manager` |

### Ethernet Bring-Up Dependency

```text
System Clock
    ↓
GPIO Alternate Function
    ↓
RMII Clock Check
    ↓
MDIO/MDC PHY Access
    ↓
DP83848 Init
    ↓
HAL ETH
    ↓
lwIP
    ↓
Network Manager
```

### Ethernet Bring-Up Checklist

| Check | Expected Result |
|-------|-----------------|
| 50 MHz RMII clock | Present |
| PHY ID read | Valid DP83848 ID |
| Link LED | Active when cable connected |
| DHCP | IP acquired |
| Ping | Gateway reachable |

---

## 4.2 Wi-Fi / BLE Interface

| Item | Value |
|------|-------|
| Module | ST67W611 |
| Main Interface | SPI |
| Control Pins | EN, BOOT, WAKEUP, RDY |
| Driver | `wifi_driver` (ST67W611 via SPI1) |
| Managers | `network_manager` |
| Architecture | T02 - NCP with LwIP on Host |

### Wi-Fi/BLE Bring-Up Dependency

```text
GPIO Init
    ↓
SPI Init
    ↓
ST67 Enable / Boot Pin Setup
    ↓
RDY Pin Interrupt
    ↓
SPI Transport
    ↓
Wi-Fi/BLE Protocol
    ↓
Network Manager
```

---

## 4.3 LTE Interface

| Item | Value |
|------|-------|
| Module | A7680C |
| Control Interface | UART1 (PB6-RX, PB7-TX) |
| Data Mode | PPPoS (Point-to-Point Protocol over Serial) |
| Driver | `pppos_driver` / `lte_driver` |
| Manager | `network_manager` |
| Features | Hard Reset, Auto-reconnect, GPDMA Idle Detection |

### LTE State Machine

```
IDLE → POWER_ON → INIT → AT_READY → NETWORK_SEARCH → REGISTERED → CONNECTING → PPP_MODE → CONNECTED
```

### LTE Bring-Up Dependency

```text
GPIO Init
    ↓
UART1 Init + GPDMA
    ↓
PWRKEY Sequence (1200ms pulse)
    ↓
AT Test
    ↓
SIM Check (AT+CPIN?)
    ↓
Network Registration (AT+CREG? / AT+CEREG?)
    ↓
PDP Context (AT+CGDCONT)
    ↓
PPP Dial (ATD*99#)
    ↓
lwIP PPP netif
    ↓
Network Manager (Failover)
```

---

## 4.4 RS485 Interface

| Item | Value |
|------|-------|
| Channels | 3 |
| Transceiver | MAX485 |
| Mode | Half-duplex |
| Protocol | Modbus RTU |
| Driver | `rs485_driver` |
| Manager | `modbus_manager` |

### RS485 Transmit Sequence

```text
Set DE = TX
    ↓
UART DMA TX
    ↓
Wait TX Complete
    ↓
Wait UART TC Flag
    ↓
Set DE = RX
```

---

## 4.5 Storage Interface

| Storage | Interface | Purpose | Firmware Owner |
|---------|-----------|---------|----------------|
| Internal Flash | Internal | Bootloader / Application | bootloader |
| W25Q128 | SPI | Config / certificates | storage_manager |
| MX25LM51245 | OctoSPI | OTA / Web / Logs | storage_manager |

---

# 5. Firmware Ownership Summary

| Hardware Block | Driver | Manager | Application User |
|---------------|--------|---------|------------------|
| Ethernet | eth_driver | network_manager | MQTT / HTTP / OTA / Modbus TCP |
| Wi-Fi (ST67W611) | wifi_driver | network_manager | Network / Provisioning |
| LTE (A7680C) | pppos_driver | network_manager | MQTT / HTTP / OTA |
| RS485 | rs485_driver | modbus_manager | Gateway App |
| SPI Flash | spi_flash_driver | storage_manager | Config / Certificate |
| OctoSPI Flash | ospi_flash_driver | storage_manager | OTA / Web |
| USB | usb_driver | system_manager | Debug / DFU |
| Debug UART | uart_driver | logger | Diagnostics |

---

# 6. Recommended Initialization Order

```text
HAL_Init
    ↓
SystemClock_Config
    ↓
GPIO_Init
    ↓
Debug_UART_Init
    ↓
DMA_Init
    ↓
SPI_Flash_Init
    ↓
OctoSPI_Flash_Init
    ↓
Storage_Manager_Init
    ↓
Config_Manager_Init
    ↓
RS485_Init
    ↓
Ethernet_Init
    ↓
USB_Device_Init
    ↓
LTE_Init
    ↓
ST67_WiFi_BLE_Init
    ↓
Network_Manager_Init
    ↓
Application_Start
```

---

# 7. Network Interface Priority

| Priority | Interface | Notes |
|----------|-----------|-------|
| 1 | Ethernet | Preferred primary network |
| 2 | Wi-Fi | Secondary network |
| 3 | LTE | Cellular backup network |

Applications must not bind directly to Ethernet, Wi-Fi or LTE.  
All application network access must go through `network_manager`.

---

# 8. Open Verification Items

These items must be verified during firmware bring-up.

| Item | Status |
|------|--------|
| STM32 alternate function selection in CubeMX | To verify |
| Ethernet PHY address | To verify |
| Ethernet REFCLK direction/source | To verify |
| ST67 SPI mode and max clock | To verify |
| ST67 UART purpose | To verify |
| A7680C UART baudrate | To verify |
| LTE PWRKEY active level and timing | To verify |
| LTE RESET active level and timing | To verify |
| RS485 DE active polarity | To verify |
| USB routing with LTE USB nets | To verify |
| W25Q128 JEDEC ID | To verify |
| MX25LM51245 OctoSPI mode | To verify |

---

# 9. AI Development Notes

AI assistants must follow these rules when generating firmware:

1. Use the MCU pin assignment table above as the source of truth.
2. Do not directly access HAL peripherals from application code.
3. Use BSP and driver layers for hardware access.
4. Use Network Manager for Ethernet, Wi-Fi and LTE selection.
5. Use Modbus Manager for RS485/Modbus operations.
6. Use Storage Manager for SPI and OctoSPI flash access.
7. Use Config Manager for runtime configuration.
8. Publish driver status through Event Bus.
9. Add diagnostic logs for every bring-up step.
10. Keep all hardware-specific constants inside BSP configuration files.

---

# 10. Document Status

This document includes actual MCU pin connections extracted from the Gateway V2 schematic.

It should be updated after:

- CubeMX configuration is finalized
- Board bring-up is completed
- Ethernet PHY ID is confirmed
- LTE AT command test is completed
- ST67 SPI communication is verified
- RS485 Modbus communication is validated
