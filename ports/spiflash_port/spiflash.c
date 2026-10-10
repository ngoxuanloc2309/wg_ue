/**
 * @file spiflash.c
 * @brief SPI Flash HAL wrapper - Standard spif.c library port
 */
#include "spiflash.h"
#include "spif.h"
#include "logger.h"
#include "spi.h"
#include "main.h"

int spiflash_init(void) {
    // Configure PA15 (SPI_NSS) as GPIO Output for Software CS
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = SPI_NSS_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(SPI_NSS_GPIO_Port, &GPIO_InitStruct);
    HAL_GPIO_WritePin(SPI_NSS_GPIO_Port, SPI_NSS_Pin, GPIO_PIN_SET);

    LOGI("SPIFLASH", "Init standard spif driver...");
    if (!SPIF_Init(&bsp_spif, &hspi3, SPI_NSS_GPIO_Port, SPI_NSS_Pin)) {
        LOGE("SPIFLASH", "SPIF_Init failed!");
        return -1;
    }
    
    return 0;
}

int spiflash_read(uint32_t addr, void *buf, uint32_t len) {
    if (!SPIF_ReadAddress(&bsp_spif, addr, (uint8_t*)buf, len)) {
        return -1;
    }
    return 0;
}

int spiflash_write(uint32_t addr, const void *buf, uint32_t len) {
    uint32_t remaining = len;
    uint32_t current_addr = addr;
    uint8_t *ptr = (uint8_t*)buf;
    
    while(remaining > 0) {
        uint32_t page_offset = current_addr % SPIF_PAGE_SIZE;
        uint32_t bytes_to_end = SPIF_PAGE_SIZE - page_offset;
        uint32_t chunk = (remaining > bytes_to_end) ? bytes_to_end : remaining;
        
        uint32_t page_num = SPIF_AddressToPage(current_addr);
        if (!SPIF_WritePage(&bsp_spif, page_num, ptr, chunk, page_offset)) {
            return -1;
        }
        
        ptr += chunk;
        current_addr += chunk;
        remaining -= chunk;
    }
    
    return 0;
}

int spiflash_erase_sector(uint32_t addr) {
    uint32_t sector_num = SPIF_AddressToSector(addr);
    if (!SPIF_EraseSector(&bsp_spif, sector_num)) {
        return -1;
    }
    return 0;
}

int spiflash_erase_chip(void) {
    if (!SPIF_EraseChip(&bsp_spif)) {
        return -1;
    }
    return 0;
}
