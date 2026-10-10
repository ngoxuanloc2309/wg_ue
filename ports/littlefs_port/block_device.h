/**
 * @file block_device.h
 * @brief LittleFS block device on SPI Flash
 */
#ifndef __BLOCK_DEVICE_H__
#define __BLOCK_DEVICE_H__

#include <stdint.h>
#include "lfs.h"

int block_device_read(const struct lfs_config *cfg, lfs_block_t block, lfs_off_t off, void *buffer, lfs_size_t size);
int block_device_prog(const struct lfs_config *cfg, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size);
int block_device_erase(const struct lfs_config *cfg, lfs_block_t block);
int block_device_sync(const struct lfs_config *cfg);

int lfs_mount_fs(void);

/* External LittleFS instance */
extern lfs_t lfs;

#endif /* __BLOCK_DEVICE_H__ */
