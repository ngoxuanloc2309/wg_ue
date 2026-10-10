# Gateway V2 - Power, Clock and Memory Specification

**Document ID:** HW-003  
**Version:** 1.0  
**Status:** Draft  
**Source:** Gateway V2 schematic, `Ethernet Wifi Gateway.pdf`

---

# 1. Purpose

This document describes the power tree, clock sources, reset behavior and memory organization of the Gateway V2 hardware platform.

It is intended for:

- BSP development
- Clock configuration
- Bootloader design
- Storage manager design
- OTA design
- Firmware bring-up
- AI coding assistants

---

# 2. Power Tree

Gateway V2 is powered from a 24V DC input and generates local 5V and 3.3V rails.

```text
24V DC Input
    │
    ├── Protection / Fuse / TVS
    │
    ▼
Buck Regulator
    │
    ├── +5V
    │     │
    │     ├── USB
    │     ├── LTE Module A7680C
    │     ├── RS485 Transceivers
    │     └── Power Indicator LED
    │
    ▼
Buck Regulator
    │
    └── +3.3V
          │
          ├── STM32H563VGT6
          ├── DP83848 Ethernet PHY
          ├── ST67W611 Wi-Fi/BLE Module
          ├── W25Q128 SPI Flash
          ├── MX25LM51245 OctoSPI Flash
          ├── Oscillators
          └── Logic Signals
```

---

# 3. Power Rails

| Rail | Source | Used By | Firmware Control |
|------|--------|---------|------------------|
| +24V | External input | Main board input | No |
| +5V | Buck regulator | USB, LTE, RS485, indicators | No direct control |
| +3.3V | Buck regulator | MCU, Ethernet PHY, Wi-Fi/BLE, Flash | No direct control |
| VDDUSB | MCU USB supply | STM32 USB FS | Indirect |
| VDDA / VREF+ | MCU analog domain | ADC / reference | No |
| VBAT | MCU backup domain | RTC backup | No / optional |

---

# 4. Firmware Impact of Power Design

The firmware should assume that:

- STM32 starts after 3.3V becomes stable.
- External peripherals may need additional initialization delay after reset.
- LTE module power-on is controlled by `PWR_KEY`.
- LTE reset is controlled by `RESET_KEY`.
- ST67W611 Wi-Fi/BLE module is controlled by `ST67_EN`, `ST67_BOOT`, and `ST67_Wakeup`.
- Ethernet PHY does not have a clearly documented dedicated MCU reset pin in the current schematic and should be verified during bring-up.
- RS485 transceivers are powered from the board rails and direction is controlled by DE/RE pins.

---

# 5. Reset and Power-On Sequence

Recommended firmware-level boot sequence:

```text
Power Applied
    │
    ▼
24V Input Stable
    │
    ▼
5V Rail Stable
    │
    ▼
3.3V Rail Stable
    │
    ▼
MCU NRST Released
    │
    ▼
CPU Starts from Internal Flash
    │
    ▼
HAL_Init()
    │
    ▼
System Clock Configuration
    │
    ▼
GPIO Initialization
    │
    ▼
Debug UART Initialization
    │
    ▼
Storage and Peripheral Initialization
```

---

# 6. Clock Sources

| Clock Source | Frequency | Connected To | Purpose |
|--------------|-----------|--------------|---------|
| HSE Crystal | 16 MHz | STM32H563 | Main MCU clock source |
| LSE Crystal | 32.768 kHz | STM32H563 | RTC / low power time base |
| Ethernet Oscillator | 50 MHz | DP83848 / RMII | Ethernet RMII reference |
| ST67 32 kHz | 32.768 kHz class | ST67W611 | Wi-Fi/BLE low-power reference |

---

# 7. MCU Clock Tree

The STM32H563 uses the 16 MHz crystal as the main external high-speed clock source.

Recommended firmware clock strategy:

```text
16 MHz HSE
    │
    ▼
PLL
    │
    ├── SYSCLK
    ├── HCLK
    ├── APB Clocks
    ├── UART Clocks
    ├── SPI Clocks
    ├── USB Clock
    └── Ethernet MAC Clock
```

The final clock configuration must be generated and verified using STM32CubeMX or the STM32 clock configuration tool.

---

# 8. Low-Speed Clock

The board includes a 32.768 kHz crystal for low-speed timing.

Expected usage:

- RTC
- Timestamping
- Periodic wakeup
- Long-term timekeeping
- Watchdog/timebase support where applicable

```text
32.768 kHz LSE
    │
    ▼
RTC
    │
    ├── System time
    ├── Log timestamp
    └── Scheduled tasks
```

---

# 9. Ethernet Clock

Ethernet uses a 50 MHz oscillator associated with the DP83848 PHY.

The Ethernet RMII clock must be verified during bring-up.

```text
50 MHz Oscillator
    │
    ▼
DP83848 PHY
    │
    ▼
RMII REF_CLK
    │
    ▼
STM32 Ethernet MAC
```

Bring-up checks:

| Check | Expected Result |
|-------|-----------------|
| 50 MHz clock present | Yes |
| ETH_REF_CLK visible at MCU/PHY | Yes |
| MDIO PHY ID read | Valid DP83848 ID |
| Link LED | Active with cable |
| Ping | Success after lwIP init |

---

# 10. Peripheral Clock Ownership

| Peripheral | Clock Dependency | Firmware Owner |
|------------|------------------|----------------|
| Ethernet MAC | RMII / AHB / ETH peripheral clock | eth_driver |
| DP83848 PHY | 50 MHz oscillator | eth_driver |
| ST67W611 | SPI clock + 32 kHz reference | st67_driver |
| A7680C | UART peripheral clock | lte_driver |
| RS485 x3 | USART clocks + timer timing | rs485_driver |
| W25Q128 | SPI peripheral clock | spi_flash_driver |
| MX25LM51245 | OctoSPI peripheral clock | ospi_flash_driver |
| USB FS | USB peripheral clock | usb_driver |
| RTC | LSE 32.768 kHz | rtc_driver |

---

# 11. Memory Devices

## 11.1 Internal Flash

| Item | Value |
|------|-------|
| Device | STM32H563 internal Flash |
| Purpose | Bootloader and main firmware |
| Firmware Owner | bootloader / application |

Recommended usage:

- Bootloader
- Main application
- Factory metadata
- Firmware version information

---

## 11.2 SPI Flash

| Item | Value |
|------|-------|
| Device | W25Q128 |
| Interface | SPI |
| Purpose | Configuration, certificates, user settings |
| Firmware Owner | spi_flash_driver / storage_manager |

Recommended usage:

- Runtime configuration
- Network settings
- MQTT settings
- Device certificate
- TLS certificate
- Factory backup config
- Calibration data

---

## 11.3 OctoSPI Flash

| Item | Value |
|------|-------|
| Device | MX25LM51245 |
| Interface | OctoSPI |
| Purpose | OTA image, web resources, logs |
| Firmware Owner | ospi_flash_driver / storage_manager |

Recommended usage:

- OTA firmware image
- Web UI static files
- Data logs
- Diagnostic dump
- Future database

---

# 12. Proposed Memory Organization

Exact addresses must be finalized after confirming flash sizes and bootloader strategy.

```text
Internal Flash
    ├── Bootloader
    ├── Main Application
    ├── Factory Metadata
    └── Boot Flags

W25Q128 SPI Flash
    ├── Active Configuration
    ├── Backup Configuration
    ├── Certificates
    ├── User Settings
    └── Calibration Data

MX25LM51245 OctoSPI Flash
    ├── OTA Image Slot
    ├── Web Resources
    ├── Log Storage
    └── Diagnostic Dumps
```

---

# 13. Storage Ownership Model

Application code shall never directly access SPI or OctoSPI Flash.

Required access model:

```text
Application / Service
    ↓
Config Manager / OTA Manager / Web Resource Manager
    ↓
Storage Manager
    ↓
SPI Flash Driver / OctoSPI Flash Driver
    ↓
HAL SPI / HAL OSPI
    ↓
Flash Device
```

---

# 14. OTA Implementation (Final)

OTA sử dụng **Internal Flash** (không phải OctoSPI). Xem `Documents/Firmware/04_OTA_ARCHITECTURE.md` đầy đủ.

```c
// OTA API — apps/ota/ota.c
int  ota_init(void);
int  ota_begin(void);                    // Erase Secondary partition
int  ota_write_chunk(uint32_t offset,   // Write chunk to Secondary
                     const uint8_t *data,
                     uint32_t length);
void ota_commit(void);                  // Mark available + reset
void ota_abort(void);                   // Erase Secondary + reset state
void ota_revert_factory(void);          // Copy Factory → Secondary → swap
void ota_enter_dfu_mode(void);         // USB DFU: magic + reset
```

---

# 15. Bootloader Considerations

The bootloader should eventually support:

- Application validation
- Firmware version check
- CRC / hash verification
- OTA image selection
- Rollback
- Factory recovery
- Watchdog-safe update process

Recommended boot decision flow:

```text
MCU Reset
    │
    ▼
Bootloader Start
    │
    ├── Check Boot Flag
    ├── Validate Application
    ├── Check OTA Image
    ├── Apply OTA if needed
    └── Jump to Application
```

---

# 16. Firmware Bring-Up Order for Power / Clock / Memory

Recommended order:

```text
1. Verify 24V input
2. Verify 5V rail
3. Verify 3.3V rail
4. Verify MCU boot
5. Verify HSE 16 MHz
6. Configure PLL
7. Enable debug UART
8. Verify LSE 32.768 kHz
9. Verify SPI Flash JEDEC ID
10. Verify OctoSPI Flash ID
11. Verify Ethernet 50 MHz clock
12. Initialize storage manager
13. Load default configuration
```

---

# 17. Verification Checklist

| Item | Method | Expected Result |
|------|--------|-----------------|
| 24V input | Multimeter | Stable input |
| 5V rail | Multimeter / scope | Stable 5V |
| 3.3V rail | Multimeter / scope | Stable 3.3V |
| HSE 16 MHz | Firmware clock test / scope | Stable clock |
| LSE 32.768 kHz | RTC test | RTC running |
| Debug UART | Serial terminal | Boot log visible |
| W25Q128 | JEDEC ID read | Valid manufacturer/device ID |
| MX25LM51245 | JEDEC ID read | Valid manufacturer/device ID |
| Ethernet 50 MHz | Scope | Stable 50 MHz |
| Config storage | Write/read/CRC | Pass |
| OTA storage | Write/read/verify | Pass |

---

# 18. Known Risks and Open Items

| Risk / Open Item | Impact | Action |
|------------------|--------|--------|
| Exact STM32 clock configuration not finalized | Peripheral instability | Verify with CubeMX |
| Ethernet REFCLK direction/source needs validation | Ethernet may not start | Check with oscilloscope |
| External flash timing not verified | Storage corruption | Validate JEDEC ID and erase/write/read |
| ~~OTA partition not finalized~~ | ~~Bootloader design incomplete~~ | ✅ RESOLVED — dual-bank swap + USB DFU implemented |
| LTE power timing not verified | LTE may not boot reliably | Measure PWRKEY timing |
| ST67 boot timing not verified | Wi-Fi/BLE startup issue | Confirm with module documentation |
| USB and LTE USB routing need verification | USB mode conflict | Validate schematic and hardware |

---

# 19. AI Development Notes

AI assistants generating firmware must follow these rules:

1. Do not hardcode final PLL values unless CubeMX configuration is available.
2. Treat the 16 MHz crystal as the MCU HSE source.
3. Treat the 32.768 kHz crystal as the RTC LSE source.
4. Treat the 50 MHz oscillator as mandatory for Ethernet bring-up.
5. Access W25Q128 only through Storage Manager.
6. Access MX25LM51245 only through Storage Manager.
7. Keep all memory layout constants in one BSP/storage configuration header.
8. Add JEDEC ID checks before using external flash.
9. Add CRC/version validation for configuration.
10. Design OTA with rollback capability.

---

# 20. AI Summary

```text
Power:
24V Input → 5V rail → 3.3V rail

Main MCU:
STM32H563VGT6

Clock:
16 MHz HSE → PLL → SYSCLK
32.768 kHz LSE → RTC
50 MHz oscillator → Ethernet RMII

Memory:
Internal Flash → Bootloader / Application
W25Q128 SPI Flash → Config / Certificates
MX25LM51245 OctoSPI Flash → OTA / Web / Logs

Firmware Rule:
Application must not access flash, clocks or hardware directly.
Use BSP, Storage Manager, Config Manager and dedicated drivers.
```

---

# 21. Document Status

This document should be updated after:

- CubeMX clock configuration is finalized
- Board bring-up confirms power rails
- External flash IDs are verified
- Ethernet RMII clock is verified
- Bootloader/OTA strategy is finalized
