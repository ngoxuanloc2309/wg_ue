#ifndef BOOTLOADER_H
#define BOOTLOADER_H

#include <stdint.h>
#include <stdbool.h>
#include "boot_flash.h"
#include "boot_err.h"

#define ENTER_BOOT_UPDATE_MODE 0

typedef struct Bootloader {
    BootFlash_t boot_flash;
    void (*jump_to_application)(uint32_t address);
    int (*read_boot_pin)(void);
}Bootloader_t;

int bootloader_init(Bootloader_t *bootloader, void (*jump_to_application)(uint32_t address),int (*read_boot_pin)(void),BootFlashFunctions_t *boot_flash_functions);

static inline int bootloader_check_for_new_firmware(Bootloader_t *bootloader){
    return boot_flash_is_new_firmware_available(&bootloader->boot_flash);
}

static inline int bootloader_jump_to_application(Bootloader_t *bootloader){
    if (bootloader->jump_to_application == NULL)
    {
        return BOOT_ERR_INVALID_PARAM; // Error: Invalid parameters
    }
    bootloader->jump_to_application(bootloader->boot_flash.partition.primary_app_address);
    return BOOT_ERR_NO_ERROR; // Success
}

int bootloader_process(Bootloader_t *boot);

int bootloader_save_partition(Bootloader_t *boot);

int bootloader_mark_healthy(Bootloader_t *boot);

#endif // BOOTLOADER_H
