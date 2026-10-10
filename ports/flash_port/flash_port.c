/**
 * @file flash_port.c
 * @brief STM32H5 Internal Flash HAL port for Bootloader library
 *
 * STM32H5 Flash constraints:
 *  - Program granularity: 16 bytes (Quad-Word)
 *  - Erase granularity:   8KB (1 sector)
 *  - Address must be aligned to 16 bytes for write
 */
#include "flash_port.h"
#include "stm32h5xx_hal.h"
#include <string.h>

/* STM32H5 requires 16-byte (quad-word) aligned writes */
#define FLASH_WRITE_ALIGN   16U

/* ---------------------------------------------------------------------------
 * Internal helpers
 * -------------------------------------------------------------------------*/

/**
 * @brief Convert byte address to STM32H5 sector number (0-based)
 *        STM32H563 Flash starts at 0x0800_0000, 8KB sectors.
 */
static uint32_t addr_to_sector(uint32_t address) {
    return (address - 0x08000000U) / 0x2000U;
}

/* ---------------------------------------------------------------------------
 * Flash HAL functions (BootFlashFunctions_t interface)
 * -------------------------------------------------------------------------*/

static int flash_port_init(void) {
    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);
    HAL_FLASH_Lock();
    return 0;
}

/**
 * @brief Erase internal flash sectors covering [address, address+size)
 *        Size must be a multiple of BOOT_FLASH_SECTOR_SIZE (8KB).
 */
static int flash_port_erase(uint32_t address, uint32_t size) {
    uint32_t sector_error = 0;

    uint32_t start_sector = addr_to_sector(address);
    uint32_t num_sectors  = (size + 0x1FFFU) / 0x2000U; /* ceil div */

    HAL_FLASH_Unlock();
    for (uint32_t i = 0; i < num_sectors; i++) {
        uint32_t current_sector = start_sector + i;
        FLASH_EraseInitTypeDef erase_init;
        erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;
        erase_init.NbSectors = 1;

        if (current_sector < 128) {
            erase_init.Banks = FLASH_BANK_1;
            erase_init.Sector = current_sector;
        } else {
            erase_init.Banks = FLASH_BANK_2;
            erase_init.Sector = current_sector - 128;
        }

        if (HAL_FLASHEx_Erase(&erase_init, &sector_error) != HAL_OK) {
            HAL_FLASH_Lock();
            return -1;
        }
    }
    HAL_FLASH_Lock();

    return 0;
}

/**
 * @brief Write data to internal flash at given address.
 *        Pads to 16-byte boundary if size is not aligned.
 */
static int flash_port_write(uint32_t address, const uint8_t *data, uint32_t size) {
    uint32_t written = 0;
    uint8_t  aligned_buf[FLASH_WRITE_ALIGN];

    HAL_FLASH_Unlock();

    while (written < size) {
        uint32_t chunk = size - written;
        if (chunk >= FLASH_WRITE_ALIGN) {
            /* Direct 16-byte write */
            if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_QUADWORD,
                                   address + written,
                                   (uint32_t)(data + written)) != HAL_OK) {
                HAL_FLASH_Lock();
                return -1;
            }
            written  += FLASH_WRITE_ALIGN;
        } else {
            /* Pad remaining bytes to 16 bytes with 0xFF */
            memset(aligned_buf, 0xFF, FLASH_WRITE_ALIGN);
            memcpy(aligned_buf, data + written, chunk);
            if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_QUADWORD,
                                   address + written,
                                   (uint32_t)aligned_buf) != HAL_OK) {
                HAL_FLASH_Lock();
                return -1;
            }
            written += chunk;
        }
    }

    HAL_FLASH_Lock();
    return 0;
}

/**
 * @brief Read data from internal flash (simple memory copy — Flash is memory-mapped)
 */
static int flash_port_read(uint32_t address, uint8_t *data, uint32_t size) {
    memcpy(data, (const void *)address, size);
    return 0;
}

/* ---------------------------------------------------------------------------
 * Exported function table
 * -------------------------------------------------------------------------*/

BootFlashFunctions_t stm32h5_flash_functions = {
    .init  = flash_port_init,
    .erase = flash_port_erase,
    .write = flash_port_write,
    .read  = flash_port_read,
};
