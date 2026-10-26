/**
 * @file splc_mqtt_tls_port.c
 * @brief Host glue for the MQTT TLS certificate block (see splc_mqtt_tls_port.h).
 *
 * Every callback runs in the engine task and does at most one littlefs operation on a
 * 120-byte chunk. littlefs is built with LFS_THREADSAFE, so config_save() in another task
 * cannot corrupt it; it can only make a chunk wait for the lock.
 */
#include "splc_mqtt_tls_port.h"

#if SPLC_MQTT_TLS_CERT_ENABLE

#include <stdint.h>
#include <stddef.h>
#include "plc_mqtt_tls.h"
#include "file_io.h"
#include "logger.h"

static const char *TAG = "SPLC_TLS";

#define TLS_DIR            "certs"
#define TLS_LFS_ERR_NOSPC  (-28)    /* LFS_ERR_NOSPC: file_io.h does not expose littlefs' codes */

/* Index = cert_type (1 Root CA, 2 client cert, 3 client key); index 0 unused. */
static const char *const k_final_path[4] = {
    NULL, TLS_DIR "/ca.pem", TLS_DIR "/client.crt", TLS_DIR "/client.key"
};
static const char *const k_tmp_path[4] = {
    NULL, TLS_DIR "/ca.pem.tmp", TLS_DIR "/client.crt.tmp", TLS_DIR "/client.key.tmp"
};
static const char *const k_name[4] = { "?", "Root CA", "client cert", "client key" };

/* The transfer in progress (the library sends one at a time). 0 = none. */
static uint8_t  s_open_type;
static uint16_t s_total_size;
static uint16_t s_written;

static bool type_valid(uint8_t type)
{
    return (type >= PLC_MQTT_TLS_TYPE_ROOT_CA) && (type <= PLC_MQTT_TLS_TYPE_CLIENT_KEY);
}

const char *splc_mqtt_tls_port_path(uint8_t type)
{
    return type_valid(type) ? k_final_path[type] : NULL;
}

static void discard_open_transfer(void)
{
    if (type_valid(s_open_type)) {
        (void)file_io_remove(k_tmp_path[s_open_type]);
    }
    s_open_type  = 0U;
    s_total_size = 0U;
    s_written    = 0U;
}

/* ---- Host functions registered with plc_mqtt_tls ---------------------------------------- */

static int tls_begin(void *ctx, uint8_t type, uint8_t total_chunks, uint16_t total_size)
{
    (void)ctx;
    if (!type_valid(type)) {
        return (int)PLC_MQTT_TLS_ST_FLASH;
    }
    /* A restart (chunk 0 again) or a transfer left over from a power cut: start clean. */
    if (file_io_mkdir(TLS_DIR) != 0 || file_io_remove(k_tmp_path[type]) != 0) {
        LOGE(TAG, "begin %s: cannot prepare %s", k_name[type], k_tmp_path[type]);
        s_open_type = 0U;
        return (int)PLC_MQTT_TLS_ST_FLASH;
    }
    s_open_type  = type;
    s_total_size = total_size;
    s_written    = 0U;
    LOGI(TAG, "begin %s: %u chunks, %u bytes", k_name[type],
         (unsigned)total_chunks, (unsigned)total_size);
    return 0;
}

static int tls_chunk(void *ctx, uint8_t type, uint8_t index, const uint8_t *data, uint8_t len)
{
    (void)ctx;
    (void)index;
    if (type != s_open_type || !type_valid(type) || data == NULL || len == 0U) {
        return (int)PLC_MQTT_TLS_ST_FLASH;
    }
    int ret = file_io_append(k_tmp_path[type], data, len);
    if (ret == TLS_LFS_ERR_NOSPC) {
        LOGE(TAG, "chunk %u of %s: littlefs full", (unsigned)index, k_name[type]);
        return (int)PLC_MQTT_TLS_ST_OVERFLOW;
    }
    if (ret != (int)len) {
        LOGE(TAG, "chunk %u of %s: append failed (%d)", (unsigned)index, k_name[type], ret);
        return (int)PLC_MQTT_TLS_ST_FLASH;
    }
    s_written = (uint16_t)(s_written + len);
    return 0;
}

static int tls_commit(void *ctx, uint8_t type)
{
    (void)ctx;
    if (type != s_open_type || !type_valid(type)) {
        return (int)PLC_MQTT_TLS_ST_FLASH;
    }
    /* The library already counted the bytes; check what is really in the file. */
    int stored = file_io_size(k_tmp_path[type]);
    if (s_written != s_total_size || stored != (int)s_total_size) {
        LOGE(TAG, "commit %s: %u of %u bytes counted, %d stored", k_name[type],
             (unsigned)s_written, (unsigned)s_total_size, stored);
        return (int)PLC_MQTT_TLS_ST_FLASH;
    }
    /* Atomic replace: the previous certificate stays valid when this fails. */
    if (file_io_rename(k_tmp_path[type], k_final_path[type]) != 0) {
        LOGE(TAG, "commit %s: rename failed, previous certificate kept", k_name[type]);
        return (int)PLC_MQTT_TLS_ST_FLASH;
    }
    LOGI(TAG, "commit %s: %u bytes stored", k_name[type], (unsigned)s_total_size);
    s_open_type  = 0U;
    s_total_size = 0U;
    s_written    = 0U;
    return 0;
}

static void tls_abort(void *ctx)
{
    (void)ctx;
    if (type_valid(s_open_type)) {
        LOGW(TAG, "abort %s transfer", k_name[s_open_type]);
    }
    discard_open_transfer();
}

static void tls_clear(void *ctx)
{
    (void)ctx;
    discard_open_transfer();
    for (uint8_t t = PLC_MQTT_TLS_TYPE_ROOT_CA; t <= PLC_MQTT_TLS_TYPE_CLIENT_KEY; t++) {
        (void)file_io_remove(k_tmp_path[t]);
        if (file_io_remove(k_final_path[t]) != 0) {
            LOGE(TAG, "clear: cannot remove %s", k_final_path[t]);
        }
    }
    LOGI(TAG, "stored certificates cleared");
}

static uint16_t tls_active_mask(void *ctx)
{
    (void)ctx;
    uint16_t mask = 0U;
    for (uint8_t t = PLC_MQTT_TLS_TYPE_ROOT_CA; t <= PLC_MQTT_TLS_TYPE_CLIENT_KEY; t++) {
        if (file_io_exists(k_final_path[t])) {
            mask |= (uint16_t)(1U << (t - 1U));    /* bit0 Root CA, bit1 cert, bit2 key */
        }
    }
    return mask;
}

void splc_mqtt_tls_port_init(void)
{
    splc_mqtt_tls_ops_t ops = {
        .ctx         = NULL,
        .begin       = tls_begin,
        .chunk       = tls_chunk,
        .commit      = tls_commit,
        .abort       = tls_abort,
        .clear       = tls_clear,
        .active_mask = tls_active_mask,
    };
    if (!splc_mqtt_tls_register(&ops)) {
        LOGE(TAG, "registration with plc_mqtt_tls failed");
        return;
    }
    LOGI(TAG, "TLS certificate storage ready (littlefs %s/)", TLS_DIR);
}

#endif /* SPLC_MQTT_TLS_CERT_ENABLE */