#ifndef FLASH_DEFINE_H
#define FLASH_DEFINE_H

/*
 * Flash Layout for GatewayV2 — STM32H563 (2MB Internal Flash)
 * Sector size: 8KB each, 256 sectors total = 2048KB
 *
 * ┌────────────────┐ 0x0800_0000
 * │   Bootloader   │  7 sectors =  56KB
 * ├────────────────┤ 0x0800_E000
 * │ Partition Table│  1 sector  =   8KB
 * ├────────────────┤ 0x0801_0000  ← App ORIGIN (linker script)
 * │  Primary App   │ 82 sectors = 656KB  (App running)
 * ├────────────────┤ 0x080B_4000
 * │  Secondary App │ 82 sectors = 656KB  (OTA new firmware)
 * ├────────────────┤ 0x0815_8000
 * │  Factory App   │ 82 sectors = 656KB  (Recovery firmware)
 * ├────────────────┤ 0x081F_C000
 * │    Scratch     │  2 sectors =  16KB  (Swap buffer)
 * └────────────────┘ 0x0820_0000  ← End of 2MB Flash ✓
 */

#define SECONDARY_FLASH     1
#define FACTORY_APP_FLASH   1
#define SCRATCH_FLASH       1

#define FLASH_NUMBER_OF_SECTORS     256
#define BOOT_FLASH_SECTOR_SIZE      0x2000  /* 8KB per sector on STM32H5 */

/* Bootloader: 7 sectors × 8KB = 56KB */
#define BOOTLOADER_FLASH_START_ADDRESS      0x08000000
#define BOOTLOADER_FLASH_NUMBER_OF_SECTORS  7
#define BOOTLOADER_FLASH_SIZE               (BOOTLOADER_FLASH_NUMBER_OF_SECTORS * BOOT_FLASH_SECTOR_SIZE)

/* Partition Table: 1 sector × 8KB = 8KB */
#define PARTITION_FLASH_START_ADDRESS       (BOOTLOADER_FLASH_START_ADDRESS + BOOTLOADER_FLASH_SIZE)
#define PARTITION_FLASH_NUMBER_OF_SECTORS   1
#define PARTITION_FLASH_SIZE                (PARTITION_FLASH_NUMBER_OF_SECTORS * BOOT_FLASH_SECTOR_SIZE)

/* Primary App: 82 sectors × 8KB = 656KB — starts at 0x0801_0000 */
#define PRIMARY_APP_FLASH_START_ADDRESS     (PARTITION_FLASH_START_ADDRESS + PARTITION_FLASH_SIZE)
#define PRIMARY_APP_FLASH_NUMBER_OF_SECTORS 82
#define PRIMARY_APP_FLASH_SIZE              (PRIMARY_APP_FLASH_NUMBER_OF_SECTORS * BOOT_FLASH_SECTOR_SIZE)

/* Secondary App: 82 sectors × 8KB = 656KB — starts at 0x080B_4000 */
#define SECONDARY_APP_FLASH_START_ADDRESS   (PRIMARY_APP_FLASH_START_ADDRESS + PRIMARY_APP_FLASH_SIZE)
#define SECONDARY_APP_FLASH_NUMBER_OF_SECTORS 82
#define SECONDARY_APP_FLASH_SIZE            (SECONDARY_APP_FLASH_NUMBER_OF_SECTORS * BOOT_FLASH_SECTOR_SIZE)

/* Factory App: 82 sectors × 8KB = 656KB — starts at 0x0815_8000 */
#define FACTORY_APP_FLASH_START_ADDRESS     (SECONDARY_APP_FLASH_START_ADDRESS + SECONDARY_APP_FLASH_SIZE)
#define FACTORY_APP_FLASH_NUMBER_OF_SECTORS 82
#define FACTORY_APP_FLASH_SIZE              (FACTORY_APP_FLASH_NUMBER_OF_SECTORS * BOOT_FLASH_SECTOR_SIZE)

/* Scratch: 2 sectors × 8KB = 16KB — starts at 0x081F_C000 */
#define SCRATCH_FLASH_START_ADDRESS         (FACTORY_APP_FLASH_START_ADDRESS + FACTORY_APP_FLASH_SIZE)
#define SCRATCH_FLASH_NUMBER_OF_SECTORS     2
#define SCRATCH_FLASH_SIZE                  (SCRATCH_FLASH_NUMBER_OF_SECTORS * BOOT_FLASH_SECTOR_SIZE)

/* Sanity check: total must equal 2048KB = 0x200000 */
/* 56 + 8 + 656 + 656 + 656 + 16 = 2048KB ✓ */

#define CFG_BOOT_MCU OPT_MCU_STM32H5

#endif // FLASH_DEFINE_H