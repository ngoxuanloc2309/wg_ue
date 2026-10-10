/**
 * @file ota.h
 * @brief Firmware OTA Update — uses Bootloader library for partition management
 *
 * OTA Flow:
 *  1. Receive firmware chunks (via MQTT or HTTP)
 *  2. ota_begin()        — erase Secondary partition
 *  3. ota_write_chunk()  — write chunks sequentially
 *  4. ota_commit()       — mark isNewFirmwareAvailable, save partition, reset
 *
 * Recovery:
 *  - ota_revert_factory() — copy Factory App → Primary, reset
 *
 * DFU Mode (USB):
 *  - ota_enter_dfu_mode() — stop USB RNDIS, set magic word, reset into Bootloader DFU
 */
#ifndef __OTA_H__
#define __OTA_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ---- OTA Write Session ---- */
typedef enum {
    OTA_STATE_IDLE = 0,
    OTA_STATE_DOWNLOADING,
    OTA_STATE_VERIFYING,
    OTA_STATE_PENDING_BOOT,
    OTA_STATE_BOOTING,
    OTA_STATE_CONFIRMED,
    OTA_STATE_ERROR
} ota_state_t;

typedef struct {
    ota_state_t state;
    uint32_t expected;
    uint32_t received;
    uint32_t version;
    int32_t error_code;
} ota_status_t;

/**
 * @brief Get the maximum allowed firmware size (Secondary partition size)
 * @return Maximum size in bytes
 */
size_t ota_get_max_fw_size(void);

/**
 * @brief Begin OTA session — erases Secondary partition.
 * @return 0 on success, -1 on failure
 */
int ota_begin(uint32_t expected_size, uint32_t version);

/**
 * @brief Write one chunk of firmware data to Secondary partition.
 * @param offset  Byte offset from start of Secondary partition
 * @param data    Pointer to data buffer
 * @param length  Length of data in bytes
 * @return 0 on success, -1 on failure
 */
int ota_write_chunk(uint32_t offset, const uint8_t *data, uint32_t length);

/**
 * @brief Commit OTA — marks new firmware available, saves partition table, resets MCU.
 * @note  MCU will reset after this call. Does not return.
 */
int ota_finalize(const uint8_t expected_sha256[32]);
int ota_commit(void);

/**
 * @brief Abort current OTA session without resetting.
 */
void ota_abort(void);
void ota_mark_healthy(void);
void ota_get_status(ota_status_t *status);

/* ---- Recovery ---- */

/**
 * @brief Revert to Factory firmware — copies Factory → Primary, then resets.
 * @note  MCU will reset after this call. Does not return.
 */
void ota_revert_factory(void);

/* ---- DFU Mode ---- */

/**
 * @brief Enter USB DFU mode.
 *        Stops TinyUSB RNDIS stack cleanly, writes magic word to RAM,
 *        then resets MCU. Bootloader will detect magic and stay in DFU mode.
 * @note  MCU will reset after this call. Does not return.
 */
void ota_enter_dfu_mode(void);

/* ---- Init ---- */
int ota_init(void);

#endif /* __OTA_H__ */
