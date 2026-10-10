/**
 * @file spiflash.h
 * @brief SPI Flash HAL wrapper
 */
#ifndef __SPIFLASH_PORT_H__
#define __SPIFLASH_PORT_H__

#include <stdint.h>

int spiflash_init(void);
int spiflash_read(uint32_t addr, void *buf, uint32_t len);
int spiflash_write(uint32_t addr, const void *buf, uint32_t len);
int spiflash_erase_sector(uint32_t addr);
int spiflash_erase_chip(void);

#endif /* __SPIFLASH_PORT_H__ */
