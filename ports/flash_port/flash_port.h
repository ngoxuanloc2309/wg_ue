/**
 * @file flash_port.h
 * @brief STM32H5 Internal Flash HAL port for Bootloader library
 */
#ifndef __FLASH_PORT_H__
#define __FLASH_PORT_H__

#include "boot_flash.h"

/**
 * @brief Pre-initialized BootFlashFunctions_t for STM32H5 Internal Flash.
 *        Pass this to bootloader_init() or ota_init_flash().
 */
extern BootFlashFunctions_t stm32h5_flash_functions;

#endif /* __FLASH_PORT_H__ */
