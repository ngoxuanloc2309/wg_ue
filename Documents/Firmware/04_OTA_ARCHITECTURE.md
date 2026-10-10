# OTA Firmware Update Architecture

**Document ID:** FW-004
**Version:** 1.0
**Status:** Final
**Target:** GatewayV2 — STM32H563VI (2MB Internal Flash)

---

# 1. Overview

GatewayV2 hỗ trợ **Dual-Path OTA** với hai cơ chế update:

| Path | Mechanism | Use Case |
|------|-----------|----------|
| **Network OTA** | Dual-bank swap qua bootloader | Cập nhật firmware từ xa qua HTTP |
| **USB DFU** | Single-bank direct write | Recovery khi network OTA fail |

> **Current bring-up mode (2026-07-20):** the application is flashed directly at
> `0x08000000` because the bootloader is currently disabled for runtime validation.
> The linker script and root `flash.bat` must be used for this mode. The partitioned
> bootloader layout below remains the OTA/production design and must not be mixed
> with a direct-boot image.

---

# 2. Flash Layout

STM32H563VI có **2MB Internal Flash**, tổ chức thành 256 sectors × 8KB.

```
┌─────────────────────┐ 0x0800_0000
│    Bootloader       │  7 sectors =  56KB
├─────────────────────┤ 0x0800_E000
│  Partition Table    │  1 sector  =   8KB
├─────────────────────┤ 0x0801_0000
│   Primary App       │ 82 sectors = 656KB  ← App đang chạy
├─────────────────────┤ 0x080B_4000
│  Secondary App      │ 82 sectors = 656KB  ← OTA new firmware
├─────────────────────┤ 0x0815_8000
│   Factory App      │ 82 sectors = 656KB  ← Recovery firmware
├─────────────────────┤ 0x081F_C000
│     Scratch         │  2 sectors =  16KB  ← Swap buffer
└─────────────────────┘ 0x0820_0000   ← End of 2MB Flash
```

**Cross-bank boundary**: Primary App (Bank 1, sectors 8-89), Secondary App (Bank 1 sectors 90-127 + Bank 2 sectors 0-43).

---

# 3. Module Ownership

```
┌──────────────────────────────────────────────────────────────┐
│  apps/ota/ota.c — OTA state machine & public API            │
│    ota_begin() / ota_write_chunk() / ota_commit()          │
│    ota_abort() / ota_revert_factory() / ota_enter_dfu_mode()│
└──────────────────────┬─────────────────────────────────────┘
                       │ boot_flash_*()
┌──────────────────────▼─────────────────────────────────────┐
│  libs/bootloader/boot_flash.c — Partition read/write/erase  │
│    flash_erase() / flash_write() / flash_read()           │
└──────────────────────┬─────────────────────────────────────┘
                       │ HAL_FLASH_*
┌──────────────────────▼─────────────────────────────────────┐
│  ports/flash_port/flash_port.c — STM32H5 HAL binding         │
│    flash_port_erase() / flash_port_write() / flash_port_read()
└─────────────────────────────────────────────────────────────┘
```

| Module | File | Responsibility |
|--------|------|---------------|
| OTA Service | `apps/ota/ota.c` | OTA state machine, public API |
| Boot Flash Lib | `libs/bootloader/boot_flash.c` | Partition I/O abstraction |
| Bootloader | `libs/bootloader/bootloader.c` | Swap logic, boot decision |
| Flash Port | `ports/flash_port/flash_port.c` | STM32H5 HAL binding |
| DFU Callbacks | `bootloader/Core/Src/main.c` | TinyUSB DFU handlers |

---

# 4. Network OTA Flow

## 4.1 End-to-End Flow

```
┌──────────┐     HTTP POST /api/firmware_update      ┌─────────────┐
│  Client  │ ──────────────────────────────────────►│  Mongoose   │
└──────────┘     (auth: admin, Content-Length: N)     │  HTTPsrv   │
                                                            │
                                         start_uploads()    │
                                         glue_ota_begin()   │
                                               │           │
                                         ┌─────▼─────────────┐
                                         │   ota_begin()     │
                                         │  1. Check s_ota_in_progress │
                                         │  2. Erase Secondary  │
                                         │  3. s_ota_in_progress=true  │
                                         └─────┬─────────────┘
                                               │
                                         do_upload() loop
                                         (per 512-byte chunk)
                                               │
                                         ┌─────▼─────────────┐
                                         │ ota_write_chunk() │
                                         │ 1. Boundary check │
                                         │ 2. boot_flash_write() │
                                         │ 3. s_ota_offset += len │
                                         └─────┬─────────────┘
                                               │
                                         Upload done (200 OK)
                                         glue_ota_end()
                                         ── 500ms timer ──
                                               │
                                         ┌─────▼─────────────┐
                                         │   ota_commit()    │
                                         │ 1. isNewFirmwareAvailable=true │
                                         │ 2. isUpgradeInProgress=true    │
                                         │ 3. bootloader_save_partition() │
                                         │ 4. vTaskDelay(200)            │
                                         │ 5. NVIC_SystemReset()         │
                                         └─────────────────────┘
```

## 4.2 Bootloader After Reset

```
Bootloader starts
       │
       ▼
bootloader_init()
       │
       ├─ Read partition table from sector 7
       │
       ├─ Check magic number
       │   ├─ Mismatch → reinit partition table (first boot)
       │   └─ Match → continue
       │
       ├─ Check DFU magic word (.noinit RAM)
       │   ├─ 0xDEADBEEF → stay in DFU mode
       │   └─ Other → continue
       │
       ├─ Check isNewFirmwareAvailable
       │   ├─ TRUE → boot_swap_firmware() → jump to new app
       │   └─ FALSE → continue
       │
       ├─ Check isUpgradeInProgress (DFU mode flag)
       │   ├─ TRUE → stay in bootloader (DFU mode)
       │   └─ FALSE → jump to Primary App
       │
       └─ Jump to application
```

## 4.3 Swap Mechanism

Bootloader thực hiện **3-step atomic swap** sử dụng Scratch làm buffer trung gian:

```
Before swap:
  Primary   = App v1.0      (running)
  Secondary = App v1.1       (new)
  Scratch   = buffer

Step 1: Primary → Scratch  (backup running app)
  Primary   = 0xFF (erased)
  Scratch   = App v1.0
  Secondary = App v1.1

Step 2: Secondary → Primary  (install new app)
  Primary   = App v1.1      ← NEW APP NOW RUNNING
  Scratch   = App v1.0
  Secondary = 0xFF (erased)

Step 3: Scratch → Secondary  (restore backup)
  Primary   = App v1.1
  Scratch   = 0xFF (erased)
  Secondary = App v1.0

After swap:
  Primary   = App v1.1 (running)
  Secondary = App v1.0 (rollback slot)
  Scratch   = empty
```

---

# 5. USB DFU Mode

## 5.1 Entering DFU Mode

```
ota_enter_dfu_mode()
       │
       ├─ tud_disconnect()        // Stop TinyUSB RNDIS
       ├─ vTaskDelay(200ms)     // Wait for host to detect disconnect
       ├─ *DFU_MAGIC_ADDR = 0xDEADBEEF  // Write to .noinit RAM
       └─ NVIC_SystemReset()
```

## 5.2 DFU Magic Word (.noinit RAM)

Magic word được lưu tại `0x20000000` — 16 bytes đầu tiên của SRAM nằm trong section `.noinit`, **không bị zero khi reset**.

```
SHARED_RAM (xrw) : ORIGIN = 0x20000000, LENGTH = 16
RAM    (xrw)    : ORIGIN = 0x20000010, LENGTH = 640K - 16
```

Both linker scripts (app và bootloader) đều có `.noinit (NOLOAD) : { KEEP(*(.noinit .noinit.*)) } >SHARED_RAM` → **cùng địa chỉ 0x20000000**.

## 5.3 DFU USB Write

```
tud_dfu_download_cb(alt, block_num, data, length)
       │
       ├─ block_num == 0 → erase Primary App (FLASH_BANK_1, sector 8, 82 sectors)
       │
       ├─ Boundary check: _write_addr + length <= APP_MAX_SIZE
       │
       └─ boot_flash_write() → flash_port_write()
           └─ HAL_FLASH_Program(QUADWORD, 16-byte aligned)
```

---

# 6. Recovery: Factory Revert

```
ota_revert_factory()
       │
       ├─ Validate Factory slot (MSP != 0xFFFFFFFF, reset_handler != 0xFFFFFFFF)
       │   └─ Fail → return (do not reset)
       │
       ├─ Erase Secondary
       │
       ├─ Copy Factory → Secondary (256-byte chunks)
       │   └─ Fail → return (do not reset)
       │
       ├─ isNewFirmwareAvailable = true
       ├─ isUpgradeInProgress = true
       ├─ bootloader_save_partition()
       └─ NVIC_SystemReset()
           │
           └─ Bootloader swap: Factory-in-Secondary → Primary
```

---

# 7. Public API

```c
// Init (gọi trong gateway.c trước khi tạo task)
int ota_init(void);                        // Init bootloader lib

// OTA Session
int  ota_begin(void);                     // Erase Secondary, bắt đầu session
int  ota_write_chunk(uint32_t offset,      // Ghi chunk vào Secondary
                     const uint8_t *data,
                     uint32_t length);
void ota_commit(void);                     // Mark available, save partition, reset

// Abort
void ota_abort(void);                      // Erase Secondary, reset state

// Recovery
void ota_revert_factory(void);             // Copy Factory → Secondary → swap

// DFU
void ota_enter_dfu_mode(void);             // tud_disconnect + magic + reset
```

---

# 8. Error Handling

| Scenario | Handling |
|---------|----------|
| Write chunk fail | HTTP 400 → `ota_abort()` → erase Secondary |
| Upload incomplete (size mismatch) | `glue_ota_end()` skip commit, reset state |
| Flash erase fail | `ota_begin()` return -1, HTTP 400 |
| Flash write fail | `ota_write_chunk()` return -1, HTTP 400 |
| Factory slot invalid | `ota_revert_factory()` return (no reset) |
| Partition save fail | `ota_commit()` return (no reset) |

---

# 9. Security

- **Auth gate**: `/api/firmware_update` yêu cầu `write_level=7` (admin-only)
- **Size validation**: `total_size > SECONDARY_APP_FLASH_SIZE` → reject before erase
- **Vector table check**: Bootloader validates MSP và reset_handler trước khi jump
- **DFU isolation**: Magic word trong `.noinit` → survives reset, không thể spoof từ app khác
- **Scratch protection**: Bootloader swap không bao giờ erase Primary trước khi Secondary đã valid

---

# 10. Key Implementation Details

## 10.1 Cross-Bank Erase

Secondary App span sectors 90-171 (crosses Bank 1 → Bank 2 boundary). Per-sector erase loop:

```c
for (i = 0; i < num_sectors; i++) {
    current_sector = start_sector + i;
    if (current_sector < 128) {
        erase.Banks = FLASH_BANK_1;
        erase.Sector = current_sector;
    } else {
        erase.Banks = FLASH_BANK_2;
        erase.Sector = current_sector - 128;  // HAL expects 0-127
    }
    HAL_FLASHEx_Erase(&erase, &sector_error);
}
```

## 10.2 Non-Aligned Write

Flash write granularity = 16 bytes (QUADWORD). Partial last chunk được pad với 0xFF:

```c
if (chunk >= 16) {
    // Direct write
    written += 16;
} else {
    // Pad to 16 bytes
    memset(aligned_buf, 0xFF, 16);
    memcpy(aligned_buf, data + written, chunk);
    written += chunk;  // NOT 16!
}
```

## 10.3 Swap Flags Cleanup

Sau khi swap thành công, bootloader phải clear cả hai flag trước khi save partition:

```c
boot->boot_flash.partition.isNewFirmwareAvailable = false;
boot->boot_flash.partition.isUpgradeInProgress = false;
bootloader_save_partition(boot);
```

---

# 11. File Map

```
GatewayV2/
├── apps/ota/
│   ├── ota.c         # OTA state machine + public API
│   └── ota.h         # Public interface
│
├── libs/bootloader/
│   ├── bootloader.c   # Init, swap, boot decision
│   ├── boot_flash.c  # Partition I/O (erase/write/read)
│   ├── flash_define.h # Flash layout constants
│   └── boot_flash.h   # BootFlashFunctions_t interface
│
├── ports/flash_port/
│   └── flash_port.c  # STM32H5 HAL binding
│
├── bootloader/              # Separate build target
│   ├── Core/Src/main.c    # TinyUSB DFU callbacks + goto_application
│   └── STM32H563xx_FLASH.ld # Linker script (.noinit section)
│
└── STM32H563xx_FLASH.ld    # App linker script (.noinit section)
```

---

# 12. Build Targets

| Target | Output | Description |
|--------|--------|-------------|
| `GatewayV2` | `build/GatewayV2.bin` | Main application (runs from Primary) |
| `Bootloader` | `build/Bootloader.bin` | DFU bootloader (runs from sector 0-6) |
