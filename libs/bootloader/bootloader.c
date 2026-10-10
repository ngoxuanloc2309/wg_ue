#include "bootloader.h"
#include "boot_flash.h"
#include "flash_define.h"
#include "boot_debug.h"
#include "sha256.h"
#include <string.h>

static uint32_t metadata_crc(const BootFlashPartition_t *partition)
{
    BootFlashPartition_t copy = *partition;
    uint32_t crc = 0xFFFFFFFFU;
    const uint8_t *p;
    uint32_t i;
    copy.metadata_crc32 = 0U;
    p = (const uint8_t *)&copy;
    for (i = 0U; i < sizeof(copy); ++i) {
        uint32_t bit;
        crc ^= p[i];
        for (bit = 0U; bit < 8U; ++bit) crc = (crc >> 1) ^ (0xEDB88320U & (-(int32_t)(crc & 1U)));
    }
    return ~crc;
}

static bool metadata_valid(const BootFlashPartition_t *partition)
{
    return partition->magic_number == PARTITION_MAGIC_NUMBER &&
           partition->metadata_version == BOOT_PARTITION_METADATA_VERSION &&
           partition->metadata_crc32 == metadata_crc(partition);
}

static bool image_vector_valid(uint32_t address, uint32_t size)
{
    uint32_t msp, reset;
    if (size < 8U) return false;
    msp = *(volatile uint32_t *)address;
    reset = *(volatile uint32_t *)(address + 4U);
    return msp >= 0x20000000U && msp < 0x20100000U &&
           (reset & 1U) != 0U && reset >= address && reset < address + size;
}

static bool image_hash_valid(Bootloader_t *bootloader, uint32_t address,
                             uint32_t size, const uint8_t expected[BOOT_IMAGE_HASH_SIZE])
{
    boot_sha256_ctx_t ctx;
    uint8_t block[256];
    uint8_t digest[BOOT_IMAGE_HASH_SIZE];
    uint32_t offset = 0U;
    boot_sha256_init(&ctx);
    while (offset < size) {
        uint32_t chunk = size - offset;
        if (chunk > sizeof(block)) chunk = sizeof(block);
        if (boot_flash_read(&bootloader->boot_flash, address + offset, block, chunk) != BOOT_ERR_NO_ERROR) return false;
        boot_sha256_update(&ctx, block, chunk);
        offset += chunk;
    }
    boot_sha256_final(&ctx, digest);
    return memcmp(digest, expected, BOOT_IMAGE_HASH_SIZE) == 0;
}

static int bootloader_jump_to_valid_image(Bootloader_t *bootloader)
{
    if (image_vector_valid(bootloader->boot_flash.partition.primary_app_address,
                           bootloader->boot_flash.partition.primary_app_size)) {
        bootloader->jump_to_application(bootloader->boot_flash.partition.primary_app_address);
        return BOOT_ERR_NO_ERROR;
    }
#if FACTORY_APP_FLASH
    if (image_vector_valid(bootloader->boot_flash.partition.factory_app_address,
                           bootloader->boot_flash.partition.factory_app_size)) {
        BOOT_WARN("Primary image invalid; booting Factory image");
        bootloader->jump_to_application(bootloader->boot_flash.partition.factory_app_address);
        return BOOT_ERR_NO_ERROR;
    }
#endif
    BOOT_ERROR("No valid application image available");
    return BOOT_ERR_INVALID_PARAM;
}

static void boot_partition_printf(BootFlashPartition_t *partition) {
    BOOT_DEBUG("Boot Flash Partition Info:");
    BOOT_DEBUG("Primary App Address: 0x%08X",(unsigned int) partition->primary_app_address);
    BOOT_DEBUG("Primary App Number of Sectors: %u",(unsigned int) partition->primary_app_number_of_sectors);
    BOOT_DEBUG("Primary App Size: %u bytes",(unsigned int) partition->primary_app_size);
#if SECONDARY_FLASH
    BOOT_DEBUG("Secondary App Address: 0x%08X",(unsigned int) partition->secondary_app_address);
    BOOT_DEBUG("Secondary App Number of Sectors: %u",(unsigned int) partition->secondary_app_number_of_sectors);
    BOOT_DEBUG("Secondary App Size: %u bytes",(unsigned int) partition->secondary_app_size);
#endif
#if FACTORY_APP_FLASH
    BOOT_DEBUG("Factory App Address: 0x%08X",(unsigned int) partition->factory_app_address);
    BOOT_DEBUG("Factory App Number of Sectors: %u",(unsigned int) partition->factory_app_number_of_sectors);
    BOOT_DEBUG("Factory App Size: %u bytes",(unsigned int) partition->factory_app_size);
#endif
#if SCRATCH_FLASH
    BOOT_DEBUG("Scratch Start Address: 0x%08X",(unsigned int) partition->scratch_start_address);
    BOOT_DEBUG("Scratch Number of Sectors: %u",(unsigned int) partition->scratch_number_of_sectors);
    BOOT_DEBUG("Scratch Size: %u bytes",(unsigned int) partition->scratch_size);
#endif
    BOOT_DEBUG("Is New Firmware Available: %s",(unsigned int) partition->isNewFirmwareAvailable ? "Yes" : "No");
    BOOT_DEBUG("Is Upgrade In Progress: %s",(unsigned int) partition->isUpgradeInProgress ? "Yes" : "No");
    BOOT_DEBUG("Magic Number: 0x%08X",(unsigned int) partition->magic_number);
}

#if SECONDARY_FLASH && SCRATCH_FLASH

static int copy_flash_range(Bootloader_t *bootloader, uint32_t source,
                            uint32_t destination, uint32_t size)
{
    uint8_t buffer[256];
    uint32_t offset = 0U;
    while (offset < size) {
        uint32_t chunk = size - offset;
        if (chunk > sizeof(buffer)) chunk = sizeof(buffer);
        if (boot_flash_read(&bootloader->boot_flash, source + offset, buffer, chunk) != BOOT_ERR_NO_ERROR ||
            boot_flash_write(&bootloader->boot_flash, destination + offset, buffer, chunk) != BOOT_ERR_NO_ERROR) {
            return BOOT_ERR_FLASH_WRITE_FAILED;
        }
        offset += chunk;
    }
    return BOOT_ERR_NO_ERROR;
}

static int boot_swap_firmware(Bootloader_t *bootloader)
{
    BOOT_DEBUG("Starting firmware swap process.");
    for(uint32_t i = 0;i<bootloader->boot_flash.partition.primary_app_number_of_sectors / bootloader->boot_flash.partition.scratch_number_of_sectors;i++){
        uint32_t primary_address = bootloader->boot_flash.partition.primary_app_address + i * bootloader->boot_flash.partition.scratch_size;
        uint32_t secondary_address = bootloader->boot_flash.partition.secondary_app_address + i * bootloader->boot_flash.partition.scratch_size;
        uint32_t scratch_address = bootloader->boot_flash.partition.scratch_start_address;

        // Step 1: Copy primary to scratch
        if(BOOT_ERR_NO_ERROR != boot_flash_erase(&bootloader->boot_flash,scratch_address,bootloader->boot_flash.partition.scratch_size)){
            BOOT_ERROR("Firmware swap failed during scratch erase.");
            return BOOT_ERR_FLASH_ERASE_FAILED;
        }
        if(BOOT_ERR_NO_ERROR != copy_flash_range(bootloader, primary_address, scratch_address,
                                                 bootloader->boot_flash.partition.scratch_size)){
            BOOT_ERROR("Firmware swap failed during scratch write.");
            return BOOT_ERR_FLASH_WRITE_FAILED;
        }

        // Step 2: Copy secondary to primary
        if(BOOT_ERR_NO_ERROR != boot_flash_erase(&bootloader->boot_flash,primary_address,bootloader->boot_flash.partition.scratch_size)){
            BOOT_ERROR("Firmware swap failed during primary erase.");
            return BOOT_ERR_FLASH_ERASE_FAILED;
        }
        if(BOOT_ERR_NO_ERROR != copy_flash_range(bootloader, secondary_address, primary_address,
                                                 bootloader->boot_flash.partition.scratch_size)){
            BOOT_ERROR("Firmware swap failed during primary write.");
            return BOOT_ERR_FLASH_WRITE_FAILED;
        }

        // Step 3: Copy scratch to secondary
        if(BOOT_ERR_NO_ERROR != boot_flash_erase(&bootloader->boot_flash,secondary_address,bootloader->boot_flash.partition.scratch_size)){
            BOOT_ERROR("Firmware swap failed during secondary erase.");
            return BOOT_ERR_FLASH_ERASE_FAILED;
        }
        if(BOOT_ERR_NO_ERROR != copy_flash_range(bootloader, scratch_address, secondary_address,
                                                 bootloader->boot_flash.partition.scratch_size)){
            BOOT_ERROR("Firmware swap failed during secondary write.");
            return BOOT_ERR_FLASH_WRITE_FAILED;
        }
    }
    BOOT_DEBUG("Firmware swap process completed successfully.");
    return BOOT_ERR_NO_ERROR; // Return success for now
}

#endif

int bootloader_init(Bootloader_t *bootloader, void (*jump_to_application)(uint32_t addr),int (*read_boot_pin)(void),BootFlashFunctions_t *boot_flash_functions){
    if (bootloader == NULL|| jump_to_application == NULL || boot_flash_functions == NULL || read_boot_pin == NULL)
    {
        return BOOT_ERR_INVALID_PARAM; // Error: Invalid parameters
    }
    bootloader->jump_to_application = jump_to_application;
    bootloader->read_boot_pin = read_boot_pin;
    *(volatile uint32_t *)0x2000000CU = 0xB0070010U;
    if(BOOT_ERR_NO_ERROR != boot_flash_init(&bootloader->boot_flash, boot_flash_functions)){
        return BOOT_ERR_FLASH_INIT_FAILED; // Error: Flash initialization failed
    }
    *(volatile uint32_t *)0x2000000CU = 0xB0070011U;

    *(volatile uint32_t *)0x2000000CU = 0xB0070020U;
    if(BOOT_ERR_NO_ERROR != boot_flash_read(&bootloader->boot_flash,PARTITION_FLASH_START_ADDRESS,(uint8_t*)&bootloader->boot_flash.partition,sizeof(BootFlashPartition_t))){
        return BOOT_ERR_FLASH_READ_FAILED;
    }
    *(volatile uint32_t *)0x2000000CU = 0xB0070021U;
    /* Hardware probe: preserve the magic value read by the bootloader before
     * any branch can select an application. */
    *(volatile uint32_t *)0x2000000CU = bootloader->boot_flash.partition.magic_number;
    if(bootloader->boot_flash.partition.magic_number != PARTITION_MAGIC_NUMBER){
        memset(&bootloader->boot_flash.partition, 0, sizeof(bootloader->boot_flash.partition));
        bootloader->boot_flash.partition.metadata_version = BOOT_PARTITION_METADATA_VERSION;
        bootloader->boot_flash.partition.isNewFirmwareAvailable = false;
        /* Fresh metadata is a normal boot state. Do not strand the device in
         * the bootloader/DFU loop before the first application has run. */
        bootloader->boot_flash.partition.isUpgradeInProgress = false;
        bootloader->boot_flash.partition.magic_number = PARTITION_MAGIC_NUMBER;
        bootloader->boot_flash.partition.image_state = BOOT_IMAGE_CONFIRMED;
        #if FACTORY_APP_FLASH
        bootloader->boot_flash.partition.factory_app_address = FACTORY_APP_FLASH_START_ADDRESS;
        bootloader->boot_flash.partition.factory_app_number_of_sectors = FACTORY_APP_FLASH_NUMBER_OF_SECTORS;
        bootloader->boot_flash.partition.factory_app_size = FACTORY_APP_FLASH_SIZE;
        #endif
        #if SECONDARY_FLASH
        bootloader->boot_flash.partition.secondary_app_address = SECONDARY_APP_FLASH_START_ADDRESS;
        bootloader->boot_flash.partition.secondary_app_number_of_sectors = SECONDARY_APP_FLASH_NUMBER_OF_SECTORS;
        bootloader->boot_flash.partition.secondary_app_size = SECONDARY_APP_FLASH_SIZE;
        #endif
        #if SCRATCH_FLASH
        bootloader->boot_flash.partition.scratch_start_address = SCRATCH_FLASH_START_ADDRESS;
        bootloader->boot_flash.partition.scratch_number_of_sectors = SCRATCH_FLASH_NUMBER_OF_SECTORS;
        bootloader->boot_flash.partition.scratch_size = SCRATCH_FLASH_SIZE;
        #endif
        bootloader->boot_flash.partition.primary_app_address = PRIMARY_APP_FLASH_START_ADDRESS;
        bootloader->boot_flash.partition.primary_app_number_of_sectors = PRIMARY_APP_FLASH_NUMBER_OF_SECTORS;
        bootloader->boot_flash.partition.primary_app_size = PRIMARY_APP_FLASH_SIZE;
        bootloader->boot_flash.partition.metadata_crc32 = metadata_crc(&bootloader->boot_flash.partition);
        if(BOOT_ERR_NO_ERROR != boot_flash_erase(&bootloader->boot_flash,PARTITION_FLASH_START_ADDRESS,PARTITION_FLASH_SIZE)){
            return BOOT_ERR_FLASH_ERASE_FAILED;
        }
        if(BOOT_ERR_NO_ERROR != boot_flash_write(&bootloader->boot_flash,PARTITION_FLASH_START_ADDRESS,(uint8_t*)&bootloader->boot_flash.partition,sizeof(BootFlashPartition_t))){
            return BOOT_ERR_FLASH_WRITE_FAILED;
        }
        /* Metadata initialization is not a request to remain in DFU mode.
         * Continue with the normal primary/factory image selection so the
         * first boot after flashing behaves like every later boot. */
        return bootloader_jump_to_valid_image(bootloader);
    }
    if (!metadata_valid(&bootloader->boot_flash.partition)) {
        bootloader->boot_flash.partition.isNewFirmwareAvailable = false;
        bootloader->boot_flash.partition.isUpgradeInProgress = false;
        bootloader->boot_flash.partition.image_state = BOOT_IMAGE_CONFIRMED;
        bootloader_save_partition(bootloader);
    }
    if(bootloader->read_boot_pin() == ENTER_BOOT_UPDATE_MODE){
        bootloader->boot_flash.partition.isUpgradeInProgress = true;
    }
    /* Do not trap the device because of a stale upgrade flag with no
     * pending image. Only BOOTING/ROLLBACK_REQUIRED need bootloader mode. */
    if (bootloader->boot_flash.partition.isUpgradeInProgress &&
        !bootloader->boot_flash.partition.isNewFirmwareAvailable &&
        bootloader->boot_flash.partition.image_state != BOOT_IMAGE_BOOTING &&
        bootloader->boot_flash.partition.image_state != BOOT_IMAGE_ROLLBACK_REQUIRED) {
        bootloader->boot_flash.partition.isUpgradeInProgress = false;
        (void)bootloader_save_partition(bootloader);
    }
    if(!bootloader_check_for_new_firmware(bootloader) &&
       !boot_flash_is_upgrade_in_progress(&bootloader->boot_flash) &&
       bootloader->boot_flash.partition.image_state != BOOT_IMAGE_BOOTING &&
       bootloader->boot_flash.partition.image_state != BOOT_IMAGE_ROLLBACK_REQUIRED){
        return bootloader_jump_to_valid_image(bootloader);
    }
    return BOOT_ERR_NO_ERROR; // Success
    
}
int bootloader_save_partition(Bootloader_t *bootloader){
        bootloader->boot_flash.partition.metadata_version = BOOT_PARTITION_METADATA_VERSION;
        bootloader->boot_flash.partition.metadata_crc32 = metadata_crc(&bootloader->boot_flash.partition);
    if(BOOT_ERR_NO_ERROR != boot_flash_erase(&bootloader->boot_flash,PARTITION_FLASH_START_ADDRESS,PARTITION_FLASH_SIZE)){
            BOOT_ERROR("Bootloader initialization failed due to flash erase partition failure.");
            return BOOT_ERR_FLASH_ERASE_FAILED;
        }
        if(BOOT_ERR_NO_ERROR != boot_flash_write(&bootloader->boot_flash,PARTITION_FLASH_START_ADDRESS,(uint8_t*)&bootloader->boot_flash.partition,sizeof(BootFlashPartition_t))){
            BOOT_ERROR("Bootloader initialization failed due to flash write partition failure.");
            return BOOT_ERR_FLASH_WRITE_FAILED;
        }
    BOOT_DEBUG("Partition reinitialized successfully.");
    boot_partition_printf(&bootloader->boot_flash.partition);
    return BOOT_ERR_NO_ERROR;
}
int bootloader_process(Bootloader_t *boot){
    if(bootloader_check_for_new_firmware(boot)){
#if SECONDARY_FLASH && SCRATCH_FLASH
            if (boot->boot_flash.partition.pending_image_size == 0U ||
                boot->boot_flash.partition.pending_image_size > boot->boot_flash.partition.secondary_app_size ||
                !image_vector_valid(boot->boot_flash.partition.secondary_app_address,
                                     boot->boot_flash.partition.pending_image_size) ||
                !image_hash_valid(boot, boot->boot_flash.partition.secondary_app_address,
                                  boot->boot_flash.partition.pending_image_size,
                                  boot->boot_flash.partition.pending_sha256)) {
                BOOT_ERROR("Pending firmware validation failed");
                boot->boot_flash.partition.isNewFirmwareAvailable = false;
                boot->boot_flash.partition.isUpgradeInProgress = true;
                boot->boot_flash.partition.image_state = BOOT_IMAGE_ROLLBACK_REQUIRED;
                bootloader_save_partition(boot);
                return 0;
            }
            if (boot_swap_firmware(boot) != BOOT_ERR_NO_ERROR) {
                BOOT_ERROR("Swap failed! Halting for DFU.");
                boot->boot_flash.partition.isNewFirmwareAvailable = false; // Prevent retry loop
                boot->boot_flash.partition.isUpgradeInProgress = true;     // Stay in DFU mode
                bootloader_save_partition(boot);
                return 0;
            }
            boot->boot_flash.partition.isNewFirmwareAvailable = false;
            boot->boot_flash.partition.isUpgradeInProgress = false;
            boot->boot_flash.partition.image_state = BOOT_IMAGE_BOOTING;
            boot->boot_flash.partition.boot_attempts = 0U;
            bootloader_save_partition(boot);
            bootloader_jump_to_valid_image(boot);
        #endif
        return 0;
    }
    if (boot->boot_flash.partition.image_state == BOOT_IMAGE_BOOTING) {
        if (boot->boot_flash.partition.boot_attempts >= 3U) {
            BOOT_ERROR("Firmware health-check failed; rollback required");
            if (boot_swap_firmware(boot) != BOOT_ERR_NO_ERROR) {
                boot->boot_flash.partition.image_state = BOOT_IMAGE_ROLLBACK_REQUIRED;
                boot->boot_flash.partition.isUpgradeInProgress = true;
                bootloader_save_partition(boot);
                return 0;
            }
            boot->boot_flash.partition.image_state = BOOT_IMAGE_CONFIRMED;
            boot->boot_flash.partition.isUpgradeInProgress = false;
            bootloader_save_partition(boot);
            bootloader_jump_to_valid_image(boot);
            return 0;
        }
        boot->boot_flash.partition.boot_attempts++;
        bootloader_save_partition(boot);
    }
    if(boot_flash_is_upgrade_in_progress(&boot->boot_flash)){
        return 0;
    }
    else{
        bootloader_jump_to_valid_image(boot);
        return 0;
    }
    return 0;
}

int bootloader_mark_healthy(Bootloader_t *boot)
{
    if (boot == NULL) return BOOT_ERR_INVALID_PARAM;
    boot->boot_flash.partition.image_state = BOOT_IMAGE_CONFIRMED;
    boot->boot_flash.partition.isUpgradeInProgress = false;
    boot->boot_flash.partition.boot_attempts = 0U;
    return bootloader_save_partition(boot);
}

