#include "boot_flash.h"
#include "flash_define.h"
#include <stdint.h>

#if CFG_BOOT_MCU == OPT_MCU_STM32H5
#include "stm32h5xx_hal.h"
#include "stm32h5xx_hal_flash.h"
#include <stm32h5xx_hal_flash_ex.h>
#include "boot_debug.h"

/*
 * STM32H5 stalls instruction fetches from a bank while that bank is being
 * erased/programmed.  The generic HAL implementation is not safe here: the
 * caller and the HAL helpers both live in Bank 1, while the boot metadata is
 * also in Bank 1.  Keep the complete operation (including polling) in SRAM.
 * The linker/startup already copies .RamFunc as part of .data initialisation.
 */
#define BOOT_FLASH_TIMEOUT_LOOPS 0x02000000U

static __attribute__((section(".RamFunc"), noinline)) int ram_flash_wait_ready(void)
{
    uint32_t loops = BOOT_FLASH_TIMEOUT_LOOPS;
    while ((FLASH_NS->NSSR & (FLASH_FLAG_BSY | FLASH_FLAG_WBNE | FLASH_FLAG_DBNE)) != 0U) {
        if (--loops == 0U) return BOOT_ERR_FLASH_INIT_FAILED;
    }
    if ((FLASH_NS->NSSR & FLASH_FLAG_SR_ERRORS) != 0U) {
        FLASH_NS->NSCCR = FLASH_NS->NSSR & FLASH_FLAG_SR_ERRORS;
        return BOOT_ERR_FLASH_INIT_FAILED;
    }
    if ((FLASH_NS->NSSR & FLASH_FLAG_EOP) != 0U) {
        FLASH_NS->NSCCR = FLASH_FLAG_EOP;
    }
    return BOOT_ERR_NO_ERROR;
}

static __attribute__((section(".RamFunc"), noinline)) int ram_flash_unlock(void)
{
    if ((FLASH_NS->NSCR & FLASH_CR_LOCK) != 0U) {
        FLASH_NS->NSKEYR = FLASH_KEY1;
        FLASH_NS->NSKEYR = FLASH_KEY2;
    }
    return ((FLASH_NS->NSCR & FLASH_CR_LOCK) == 0U) ?
           BOOT_ERR_NO_ERROR : BOOT_ERR_FLASH_INIT_FAILED;
}

static __attribute__((section(".RamFunc"), noinline)) void ram_flash_lock(void)
{
    FLASH_NS->NSCR |= FLASH_CR_LOCK;
}

static __attribute__((section(".RamFunc"), noinline)) int ram_flash_erase_sector(
    uint32_t sector, uint32_t bank)
{
    uint32_t control = FLASH_CR_SER | (sector << FLASH_CR_SNB_Pos);
    if (bank == FLASH_BANK_2) control |= FLASH_CR_BKSEL;

    if (ram_flash_wait_ready() != BOOT_ERR_NO_ERROR) return BOOT_ERR_FLASH_ERASE_FAILED;
    FLASH_NS->NSCR &= ~(FLASH_CR_SER | FLASH_CR_SNB | FLASH_CR_BKSEL);
    FLASH_NS->NSCR |= control;
    FLASH_NS->NSCR |= FLASH_CR_START;
    if (ram_flash_wait_ready() != BOOT_ERR_NO_ERROR) return BOOT_ERR_FLASH_ERASE_FAILED;
    FLASH_NS->NSCR &= ~FLASH_CR_SER;
    return BOOT_ERR_NO_ERROR;
}

static __attribute__((section(".RamFunc"), noinline)) int ram_flash_program_quadword(
    uint32_t address, const uint32_t *source)
{
    volatile uint32_t *destination = (volatile uint32_t *)address;
    uint32_t index;

    if (ram_flash_wait_ready() != BOOT_ERR_NO_ERROR) return BOOT_ERR_FLASH_WRITE_FAILED;
    FLASH_NS->NSCR |= FLASH_CR_PG;
    __disable_irq();
    for (index = 0U; index < 4U; ++index) destination[index] = source[index];
    __DSB();
    __ISB();
    __enable_irq();
    if (ram_flash_wait_ready() != BOOT_ERR_NO_ERROR) return BOOT_ERR_FLASH_WRITE_FAILED;
    FLASH_NS->NSCR &= ~FLASH_CR_PG;
    return BOOT_ERR_NO_ERROR;
}

int flash_init(void) {
    /* Reading the metadata does not require changing FLASH control
     * registers.  In particular, touching NSCR/NSCCR during the earliest
     * boot stage can fault under the H5 boot security context.  Defer all
     * unlock/flag handling to the actual erase/program operation. */
    return BOOT_ERR_NO_ERROR;
}

int flash_erase(uint32_t address, uint32_t size) {
    *(volatile uint32_t *)0x2000000CU = 0xEA000001U;
    if (size == 0U || (address - FLASH_BASE) % FLASH_SECTOR_SIZE != 0U ||
        size % FLASH_SECTOR_SIZE != 0U || ram_flash_unlock() != BOOT_ERR_NO_ERROR) {
        *(volatile uint32_t *)0x2000000CU = 0xEA000002U;
        return BOOT_ERR_FLASH_ERASE_FAILED;
    }
    uint32_t start_sector = (address - FLASH_BASE) / FLASH_SECTOR_SIZE;
    uint32_t num_sector = size / FLASH_SECTOR_SIZE;

    for (uint32_t i = 0; i < num_sector; i++) {
        uint32_t current_sector = start_sector + i;
        uint32_t bank;
        if (current_sector < 128) {
            bank = FLASH_BANK_1;
        } else {
            bank = FLASH_BANK_2;
            current_sector -= 128U;
        }
        if (ram_flash_erase_sector(current_sector, bank) != BOOT_ERR_NO_ERROR) {
            *(volatile uint32_t *)0x2000000CU = 0xEA000100U | i;
            ram_flash_lock();
            return BOOT_ERR_FLASH_ERASE_FAILED;
        }
    }
    ram_flash_lock();
    /* Verify the erase while still in the bootloader, before selecting an
     * application.  A successful controller poll must not be mistaken for
     * a successful memory operation. */
    {
        const volatile uint32_t *word = (const volatile uint32_t *)address;
        uint32_t i;
        for (i = 0U; i < size / sizeof(uint32_t); ++i) {
            if (word[i] != 0xFFFFFFFFU) {
                *(volatile uint32_t *)0x2000000CU = 0xE5000000U | (FLASH_NS->NSSR & 0x00FFFFFFU);
                return BOOT_ERR_FLASH_ERASE_FAILED;
            }
        }
    }
    *(volatile uint32_t *)0x2000000CU = 0xE0000000U | (FLASH_NS->NSSR & 0x00FFFFFFU);
    return BOOT_ERR_NO_ERROR;
}

int flash_write(uint32_t address, const uint8_t *data, uint32_t size) {
    *(volatile uint32_t *)0x2000000CU = 0xF0000001U;
    if (data == NULL || size == 0U || (address & 0x0FU) != 0U ||
        ram_flash_unlock() != BOOT_ERR_NO_ERROR) {
        *(volatile uint32_t *)0x2000000CU = 0xF0000002U;
        return BOOT_ERR_FLASH_WRITE_FAILED;
    }
    uint32_t address_write = address;
    uint32_t quadword[4] __attribute__((aligned(16)));
    uint32_t remaining = size;
    while(remaining > 0) {
        uint32_t chunk = (remaining >= 16) ? 16 : remaining;
        uint32_t i;
        for (i = 0U; i < 4U; ++i) quadword[i] = 0xFFFFFFFFU;
        for (i = 0U; i < chunk; ++i) {
            ((uint8_t *)quadword)[i] = data[(address_write - address) + i];
        }
        if (ram_flash_program_quadword(address_write, quadword) != BOOT_ERR_NO_ERROR) {
            *(volatile uint32_t *)0x2000000CU = 0xF0000100U | (address_write & 0xFFU);
            ram_flash_lock();
            return BOOT_ERR_FLASH_WRITE_FAILED;
        }
        address_write += 16;
        remaining -= chunk;
    }
    ram_flash_lock();
    {
        const volatile uint32_t *written = (const volatile uint32_t *)address;
        uint32_t offset;
        for (offset = 0U; offset < size; offset += 16U) {
            uint32_t i;
            uint32_t expected[4] __attribute__((aligned(16)));
            uint32_t chunk = size - offset;
            if (chunk > 16U) chunk = 16U;
            for (i = 0U; i < 4U; ++i) expected[i] = 0xFFFFFFFFU;
            for (i = 0U; i < chunk; ++i) {
                ((uint8_t *)expected)[i] = data[offset + i];
            }
            for (i = 0U; i < 4U; ++i) {
                if (written[(offset / 4U) + i] != expected[i]) {
                    *(volatile uint32_t *)0x2000000CU = 0xF5000000U | (FLASH_NS->NSSR & 0x00FFFFFFU);
                    return BOOT_ERR_FLASH_WRITE_FAILED;
                }
            }
        }
    }
    *(volatile uint32_t *)0x2000000CU = 0xF0000000U | (FLASH_NS->NSSR & 0x00FFFFFFU);
    return BOOT_ERR_NO_ERROR;
}

int flash_read(uint32_t address, uint8_t *data, uint32_t size) {
    memcpy(data, (const void *)address, size);
    return BOOT_ERR_NO_ERROR;
}

#else
#include "boot_debug.h"
int flash_init(void) {
    BOOT_DEBUG("Flash init called. No specific implementation for this MCU.");
    return BOOT_ERR_NO_ERROR;
}

int flash_erase(uint32_t address, uint32_t size) {
    BOOT_DEBUG("Flash erase called. No specific implementation for this MCU.");
    return BOOT_ERR_NO_ERROR;
}

int flash_write(uint32_t address, const uint8_t *data, uint32_t size) {
    BOOT_DEBUG("Flash write called. No specific implementation for this MCU.");
    return BOOT_ERR_NO_ERROR;
}

int flash_read(uint32_t address, uint8_t *data, uint32_t size) {
    BOOT_DEBUG("Flash read called. No specific implementation for this MCU.");
    return BOOT_ERR_NO_ERROR;
}
#endif

BootFlashFunctions_t boot_flash_functions = {
    .init = flash_init,
    .erase = flash_erase,
    .write = flash_write,
    .read = flash_read
};
