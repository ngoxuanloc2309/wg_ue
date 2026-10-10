/**
 * @file ota.c
 * @brief Firmware OTA Update implementation
 *        Uses libs_bootloader for partition management and
 *        ports_flash_port (STM32H5 HAL) for Internal Flash access.
 */
#include "ota.h"
#include "flash_define.h"
#include "bootloader.h"
#include "flash_port.h"
#include "logger.h"
#include "stm32h5xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "mbedtls/sha256.h"
#include <string.h>

/*---------------------------------------------------------------------------
 * DFU Magic Word (written to .noinit RAM — survives software reset)
 *--------------------------------------------------------------------------*/
/** Address in SRAM that is in .noinit section — not zeroed on reset */
volatile uint32_t dfu_magic_word __attribute__((section(".noinit"))) __attribute__((used));
#define DFU_MAGIC_ADDR  (&dfu_magic_word)
#define DFU_MAGIC_KEY   0xDEADBEEFU

/*---------------------------------------------------------------------------
 * Internal state
 *--------------------------------------------------------------------------*/
static const char *TAG = "OTA";

static Bootloader_t s_bootloader;
static bool         s_ota_in_progress = false;
static bool         s_ota_finalized = false;
static uint32_t     s_ota_expected = 0U;
static uint32_t     s_ota_received = 0U;
static uint32_t     s_ota_erased_offset = 0U; /* lazy sector-erase cursor */
static uint32_t     s_ota_version = 0U;
static ota_state_t  s_ota_state = OTA_STATE_IDLE;
static int32_t      s_ota_error = 0;
static SemaphoreHandle_t s_ota_mutex = NULL;
static mbedtls_sha256_context s_sha256;

static bool ota_lock(void)
{
    return s_ota_mutex != NULL &&
           xSemaphoreTake(s_ota_mutex, pdMS_TO_TICKS(1000)) == pdTRUE;
}

static void ota_unlock(void)
{
    if (s_ota_mutex != NULL) (void)xSemaphoreGive(s_ota_mutex);
}

/*---------------------------------------------------------------------------
 * Read boot pin stub — GatewayV2 has no physical BOOT0 button on app side
 * Return 1 (not pressing) so bootloader does not force DFU from pin.
 *--------------------------------------------------------------------------*/
static int read_boot_pin(void) {
    return 1;
}

/*---------------------------------------------------------------------------
 * Jump-to-application stub (not used from App side)
 *--------------------------------------------------------------------------*/
static void jump_to_app_stub(uint32_t addr) {
    (void)addr;
    /* Not used in App context — only bootloader calls this */
}

/*---------------------------------------------------------------------------
 * Public API
 *--------------------------------------------------------------------------*/

int ota_init(void) {
    if (s_ota_mutex == NULL) s_ota_mutex = xSemaphoreCreateMutex();
    int ret = bootloader_init(&s_bootloader,
                              jump_to_app_stub,
                              read_boot_pin,
                              &stm32h5_flash_functions);
    if (ret != BOOT_ERR_NO_ERROR && ret != BOOT_PARTITION_MGIC_NUMBER_MISMATCH) {
        LOGE(TAG, "Bootloader init failed: %d", ret);
        return -1;
    } else {
        LOGI(TAG, "OTA ready. New FW: %s",
             s_bootloader.boot_flash.partition.isNewFirmwareAvailable ? "YES" : "NO");
    }
    s_ota_state = s_bootloader.boot_flash.partition.image_state == BOOT_IMAGE_BOOTING
        ? OTA_STATE_BOOTING : OTA_STATE_CONFIRMED;
    return 0;
}

size_t ota_get_max_fw_size(void) {
    return SECONDARY_APP_FLASH_SIZE;
}

int ota_begin(uint32_t expected_size, uint32_t version) {
    if (s_ota_mutex == NULL || xSemaphoreTake(s_ota_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) return -2;
    if (s_ota_in_progress || expected_size == 0U || expected_size > SECONDARY_APP_FLASH_SIZE) {
        s_ota_error = -2; s_ota_state = OTA_STATE_ERROR;
        xSemaphoreGive(s_ota_mutex); return -2;
    }

    LOGI(TAG, "OTA begin — lazy sector erase enabled for Secondary (0x%08X, %u bytes)",
         (unsigned)SECONDARY_APP_FLASH_START_ADDRESS,
         (unsigned)SECONDARY_APP_FLASH_SIZE);

    s_ota_erased_offset = 0U;

    s_ota_in_progress = true;
    s_ota_finalized = false;
    s_ota_expected = expected_size;
    s_ota_received = 0U;
    s_ota_version = version;
    s_ota_error = 0;
    s_ota_state = OTA_STATE_DOWNLOADING;
    mbedtls_sha256_free(&s_sha256);
    mbedtls_sha256_init(&s_sha256);
    mbedtls_sha256_starts(&s_sha256, 0);
    LOGI(TAG, "OTA begin OK");
    xSemaphoreGive(s_ota_mutex);
    return 0;
}

int ota_write_chunk(uint32_t offset, const uint8_t *data, uint32_t length) {
    if (!ota_lock()) return -2;
    if (!s_ota_in_progress || data == NULL || length == 0U || offset != s_ota_received) {
        LOGE(TAG, "ota_write_chunk called without ota_begin");
        ota_unlock(); return -1;
    }
    if (offset + length > s_ota_expected || offset + length > SECONDARY_APP_FLASH_SIZE) {
        LOGE(TAG, "Chunk exceeds Secondary partition boundary");
        ota_unlock(); return -1;
    }

    /* Erase lazily, sector by sector, as the stream touches them.  Erasing
     * the whole Secondary partition inside ota_begin() blocked the HTTP
     * event loop for tens of seconds, starving the TCP socket until the
     * uploader aborted (~64 KB sent).  Each sector is erased exactly once
     * per OTA session. */
    {
        uint32_t sec_size = BOOT_FLASH_SECTOR_SIZE;
        uint32_t off_end = offset + length;
        while (s_ota_erased_offset < off_end) {
            int eret = boot_flash_erase(&s_bootloader.boot_flash,
                                        SECONDARY_APP_FLASH_START_ADDRESS + s_ota_erased_offset,
                                        sec_size);
            if (eret != BOOT_ERR_NO_ERROR) {
                LOGE(TAG, "Lazy erase at offset %u failed: %d",
                     (unsigned)s_ota_erased_offset, eret);
                s_ota_error = -3; s_ota_state = OTA_STATE_ERROR;
                ota_unlock(); return -1;
            }
            s_ota_erased_offset += sec_size;
        }
    }

    uint32_t target_addr = SECONDARY_APP_FLASH_START_ADDRESS + offset;
    int ret = boot_flash_write(&s_bootloader.boot_flash, target_addr, data, length);
    if (ret != BOOT_ERR_NO_ERROR) {
        LOGE(TAG, "Write chunk at offset %u failed: %d", (unsigned)offset, ret);
        ota_unlock(); return -1;
    }
    s_ota_received += length;
    mbedtls_sha256_update(&s_sha256, data, length);
    ota_unlock();
    return 0;
}

int ota_finalize(const uint8_t expected_sha256[32]) {
    uint8_t digest[32];
    uint32_t msp, reset;
    if (!ota_lock()) return -2;
    if (!s_ota_in_progress || s_ota_received != s_ota_expected) {
        ota_unlock(); return -1;
    }
    s_ota_state = OTA_STATE_VERIFYING;
    mbedtls_sha256_finish(&s_sha256, digest);
    mbedtls_sha256_free(&s_sha256);
    if (expected_sha256 != NULL && memcmp(digest, expected_sha256, sizeof(digest)) != 0) {
        s_ota_error = -4; s_ota_state = OTA_STATE_ERROR; ota_unlock(); return -4;
    }
    msp = *(volatile uint32_t *)SECONDARY_APP_FLASH_START_ADDRESS;
    reset = *(volatile uint32_t *)(SECONDARY_APP_FLASH_START_ADDRESS + 4U);
    if (msp < 0x20000000U || msp >= 0x20100000U || (reset & 1U) == 0U ||
        reset < SECONDARY_APP_FLASH_START_ADDRESS ||
        reset >= SECONDARY_APP_FLASH_START_ADDRESS + s_ota_expected) {
        s_ota_error = -5; s_ota_state = OTA_STATE_ERROR; ota_unlock(); return -5;
    }
    memcpy(s_bootloader.boot_flash.partition.pending_sha256, digest, sizeof(digest));
    s_bootloader.boot_flash.partition.pending_image_size = s_ota_expected;
    s_bootloader.boot_flash.partition.pending_image_version = s_ota_version;
    s_bootloader.boot_flash.partition.image_state = BOOT_IMAGE_VERIFIED;
    s_ota_finalized = true;
    s_ota_state = OTA_STATE_PENDING_BOOT;
    ota_unlock();
    return 0;
}

int ota_commit(void) {
    if (!ota_lock()) return -2;
    if (!s_ota_in_progress || !s_ota_finalized) {
        LOGW(TAG, "ota_commit called before successful finalize");
        ota_unlock(); return -1;
    }

    LOGI(TAG, "OTA commit — marking new firmware available");
    s_bootloader.boot_flash.partition.isNewFirmwareAvailable  = true;
    s_bootloader.boot_flash.partition.isUpgradeInProgress     = true;
    s_bootloader.boot_flash.partition.image_state = BOOT_IMAGE_PENDING_BOOT;

    int ret = bootloader_save_partition(&s_bootloader);
    if (ret != BOOT_ERR_NO_ERROR) {
        LOGE(TAG, "Save partition failed: %d — NOT resetting", ret);
        s_ota_in_progress = false;
        s_ota_error = -6;
        s_ota_state = OTA_STATE_ERROR;
        ota_unlock();
        return -6;
    }

    LOGI(TAG, "Partition saved. Resetting MCU...");
    ota_unlock();
    vTaskDelay(pdMS_TO_TICKS(200)); /* flush UART logs */
    NVIC_SystemReset();
    return 0;
}

void ota_abort(void) {
    if (!ota_lock()) return;
    /* Do not perform a long erase from an HTTP/Mongoose callback. The next
     * ota_begin() always erases the complete secondary slot before writing. */
    if (s_ota_in_progress) LOGW(TAG, "OTA aborted; secondary will be erased on next begin");
    s_ota_in_progress = false;
    s_ota_finalized = false;
    s_ota_state = OTA_STATE_IDLE;
    s_ota_error = 0;
    s_ota_expected = 0U;
    s_ota_received = 0U;
    s_ota_version = 0U;
    ota_unlock();
}

void ota_mark_healthy(void) {
    if (bootloader_mark_healthy(&s_bootloader) == BOOT_ERR_NO_ERROR) {
        s_ota_state = OTA_STATE_CONFIRMED;
        LOGI(TAG, "OTA health-check confirmed");
    }
}

void ota_get_status(ota_status_t *status) {
    if (status == NULL) return;
    status->state = s_ota_state;
    status->expected = s_ota_expected;
    status->received = s_ota_received;
    status->version = s_ota_version;
    status->error_code = s_ota_error;
}

void ota_revert_factory(void) {
    LOGI(TAG, "Reverting to Factory firmware via Bootloader swap...");

    /* Validate Factory slot before copying (Defense-in-depth) */
    uint32_t msp = *(volatile uint32_t *)FACTORY_APP_FLASH_START_ADDRESS;
    uint32_t reset_handler = *((volatile uint32_t *)(FACTORY_APP_FLASH_START_ADDRESS + 4));
    if (msp == 0xFFFFFFFF || reset_handler == 0xFFFFFFFF) {
        LOGE(TAG, "Factory slot is empty/erased. Aborting revert.");
        return;
    }

    /* Erase Secondary */
    if (boot_flash_erase(&s_bootloader.boot_flash,
                          SECONDARY_APP_FLASH_START_ADDRESS,
                          SECONDARY_APP_FLASH_SIZE) != BOOT_ERR_NO_ERROR) {
        LOGE(TAG, "Erase Secondary failed");
        return;
    }

    /* Copy Factory -> Secondary in 256-byte chunks */
    uint8_t buf[256];
    for (uint32_t off = 0; off < FACTORY_APP_FLASH_SIZE; off += sizeof(buf)) {
        uint32_t chunk = (FACTORY_APP_FLASH_SIZE - off < sizeof(buf))
                         ? (FACTORY_APP_FLASH_SIZE - off) : sizeof(buf);
        boot_flash_read(&s_bootloader.boot_flash,
                         FACTORY_APP_FLASH_START_ADDRESS + off, buf, chunk);
        if (boot_flash_write(&s_bootloader.boot_flash,
                              SECONDARY_APP_FLASH_START_ADDRESS + off, buf, chunk)
            != BOOT_ERR_NO_ERROR) {
            LOGE(TAG, "Write Secondary failed at offset %u", (unsigned)off);
            return;
        }
    }

    LOGI(TAG, "Factory -> Secondary copy done. Triggering Bootloader swap...");
    s_bootloader.boot_flash.partition.isNewFirmwareAvailable = true;
    s_bootloader.boot_flash.partition.isUpgradeInProgress = true;
    bootloader_save_partition(&s_bootloader);

    vTaskDelay(pdMS_TO_TICKS(200));
    NVIC_SystemReset();
}

void ota_enter_dfu_mode(void) {
    LOGI(TAG, "Entering USB DFU mode...");

    /*
     * Step 1: Disconnect TinyUSB cleanly so host OS doesn't get confused.
     *         usb_netif_task calls tud_task() — after disconnect the host
     *         will see the device as unplugged (RNDIS gone).
     */
    extern void tud_disconnect(void);
    tud_disconnect();
    vTaskDelay(pdMS_TO_TICKS(200));

    /*
     * Step 2: Write Magic Word into .noinit RAM.
     *         This RAM region is NOT zeroed on software reset.
     *         Bootloader checks this word before deciding whether to
     *         jump to App or stay in DFU mode.
     */
    *DFU_MAGIC_ADDR = DFU_MAGIC_KEY;

    /* Step 3: Software reset — Bootloader will take over */
    vTaskDelay(pdMS_TO_TICKS(50));
    NVIC_SystemReset();
}
