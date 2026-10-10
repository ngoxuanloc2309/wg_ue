/**
 * @file block_device.c
 * @brief LittleFS block device - SPI Flash backend
 */
#include "block_device.h"
#include "spiflash.h"
#include "spif.h"
#include "logger.h"
#include "lfs.h"
#include "FreeRTOS.h"
#include "semphr.h"

lfs_t lfs;
static SemaphoreHandle_t s_lfs_mutex = NULL;

static int lfs_lock(const struct lfs_config *c) {
    (void)c;
    if (s_lfs_mutex) xSemaphoreTake(s_lfs_mutex, portMAX_DELAY);
    return 0;
}

static int lfs_unlock(const struct lfs_config *c) {
    (void)c;
    if (s_lfs_mutex) xSemaphoreGive(s_lfs_mutex);
    return 0;
}

const struct lfs_config cfg = {
    .read  = block_device_read,
    .prog  = block_device_prog,
    .erase = block_device_erase,
    .sync  = block_device_sync,
    .read_size = 256,
    .prog_size = 256,
    .block_size = 4096,
    .block_count = 4096,
    .cache_size = 256,
    .lookahead_size = 16,
    .block_cycles = 500,
    .lock = lfs_lock,
    .unlock = lfs_unlock,
};

int block_device_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer, lfs_size_t size) {
    (void)c;
    uint32_t addr = block * c->block_size + off;
    return spiflash_read(addr, buffer, size) == 0 ? 0 : LFS_ERR_IO;
}

int block_device_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size) {
    (void)c;
    uint32_t addr = block * c->block_size + off;
    return spiflash_write(addr, buffer, size) == 0 ? 0 : LFS_ERR_IO;
}

int block_device_erase(const struct lfs_config *c, lfs_block_t block) {
    (void)c;
    uint32_t addr = block * c->block_size;
    return spiflash_erase_sector(addr) == 0 ? 0 : LFS_ERR_IO;
}

int block_device_sync(const struct lfs_config *c) {
    (void)c;
    return 0;
}

int lfs_mount_fs(void) {
    if (!s_lfs_mutex) {
        s_lfs_mutex = xSemaphoreCreateMutex();
        if (s_lfs_mutex == NULL) {
            LOGE("LittleFS", "Failed to create filesystem mutex");
            return LFS_ERR_NOMEM;
        }
    }
    int err = lfs_mount(&lfs, &cfg);
    if (err) {
        /* Never format automatically: a transient I/O fault or corrupted
         * metadata must not silently destroy configuration/data. Recovery is
         * an explicit maintenance operation. */
        LOGE("LittleFS", "Mount failed: %d (automatic format disabled)", err);
    }
    return err;
}
