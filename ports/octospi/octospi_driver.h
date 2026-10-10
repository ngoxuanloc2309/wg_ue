/**
 * @file octospi_driver.h
 * @brief OctoSPI Driver Header
 */
#ifndef __OCTOSPI_DRIVER_H__
#define __OCTOSPI_DRIVER_H__

#include <stdint.h>

int octospi_init(void);
int octospi_read(uint32_t addr, void *buf, uint32_t len);
int octospi_write(uint32_t addr, const void *buf, uint32_t len);
int octospi_erase(uint32_t addr, uint32_t len);

#endif /* __OCTOSPI_DRIVER_H__ */
