/**
 * @file splc_mqtt_tls_client.c
 * @brief Build the lwIP altcp_tls client config from the certificates on littlefs.
 *        See splc_mqtt_tls_client.h.
 *
 * ASSUMPTIONS TO CONFIRM against the real tree (marked "CHECK"):
 *   - file_io_read(path, buf, max_len) returns the number of bytes read, or < 0 on error.
 *   - file_io_size(path) and file_io_exists(path) exist (the stage 1 glue already uses them).
 *   - SPLC_TLS_ALLOC / SPLC_TLS_FREE: default to malloc/free. Replace with the heap that has
 *     room (pvPortMalloc / vPortFree on FreeRTOS), NOT lwIP's mem_malloc (MEM_SIZE is small).
 */
#include "splc_mqtt_tls_client.h"

#if SPLC_MQTT_TLS_CERT_ENABLE

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "lwip/altcp_tls.h"
#include "file_io.h"
#include "logger.h"
#include "splc_mqtt_tls_port.h"
#include "plc_mqtt_tls.h"

#ifndef SPLC_TLS_ALLOC
#define SPLC_TLS_ALLOC(n)   malloc(n)
#define SPLC_TLS_FREE(p)    free(p)
#endif

static const char *TAG = "SPLC_TLS_CLI";

/* One stored PEM, read into RAM with the terminating NUL that mbedTLS requires
 * (the length passed to lwIP must include it). */
typedef struct {
    uint8_t *data;
    size_t   len;      /* includes the NUL */
} pem_t;

static void pem_free(pem_t *p)
{
    if (p->data != NULL) {
        /* The private key must not linger in freed heap memory. */
        memset(p->data, 0, p->len);
        SPLC_TLS_FREE(p->data);
    }
    p->data = NULL;
    p->len  = 0U;
}

static bool pem_load(uint8_t type, pem_t *out)
{
    const char *path = splc_mqtt_tls_port_path(type);
    out->data = NULL;
    out->len  = 0U;
    if (path == NULL) {
        return false;
    }
    int size = file_io_size(path);
    if (size <= 0 || size > 4096) {          /* stage 1 caps a certificate at 3840 bytes */
        LOGE(TAG, "%s: bad size %d", path, size);
        return false;
    }
    uint8_t *buf = (uint8_t *)SPLC_TLS_ALLOC((size_t)size + 1U);
    if (buf == NULL) {
        LOGE(TAG, "%s: out of memory (%d bytes)", path, size + 1);
        return false;
    }
    int got = file_io_read(path, buf, (size_t)size);        /* CHECK signature */
    if (got != size) {
        LOGE(TAG, "%s: read %d of %d bytes", path, got, size);
        memset(buf, 0, (size_t)size + 1U);
        SPLC_TLS_FREE(buf);
        return false;
    }
    buf[size] = 0U;
    out->data = buf;
    out->len  = (size_t)size + 1U;
    return true;
}

bool splc_mqtt_tls_client_available(void)
{
    return file_io_exists(splc_mqtt_tls_port_path(PLC_MQTT_TLS_TYPE_ROOT_CA));
}

struct altcp_tls_config *splc_mqtt_tls_client_config_create(void)
{
    struct altcp_tls_config *cfg = NULL;
    pem_t ca = {0}, crt = {0}, key = {0};

    bool have_crt = file_io_exists(splc_mqtt_tls_port_path(PLC_MQTT_TLS_TYPE_CLIENT_CERT));
    bool have_key = file_io_exists(splc_mqtt_tls_port_path(PLC_MQTT_TLS_TYPE_CLIENT_KEY));

    if (have_crt != have_key) {
        LOGE(TAG, "client cert and key must both exist or both be absent (cert=%d key=%d)",
             (int)have_crt, (int)have_key);
        return NULL;
    }
    if (!pem_load(PLC_MQTT_TLS_TYPE_ROOT_CA, &ca)) {
        LOGE(TAG, "Root CA missing or unreadable: TLS not possible");
        return NULL;
    }

    if (have_crt) {
        if (pem_load(PLC_MQTT_TLS_TYPE_CLIENT_CERT, &crt) &&
            pem_load(PLC_MQTT_TLS_TYPE_CLIENT_KEY,  &key)) {
            cfg = altcp_tls_create_config_client_2wayauth(
                      ca.data,  ca.len,
                      key.data, key.len, NULL, 0U,      /* no key password */
                      crt.data, crt.len);
            LOGI(TAG, "mutual TLS config %s", cfg ? "created" : "FAILED (parse error or out of heap)");
        }
    } else {
        cfg = altcp_tls_create_config_client(ca.data, ca.len);
        LOGI(TAG, "one-way TLS config %s", cfg ? "created" : "FAILED (parse error or out of heap)");
    }

    /* mbedTLS copied what it needs while parsing: the PEM buffers can go. */
    pem_free(&ca);
    pem_free(&crt);
    pem_free(&key);
    return cfg;
}

void splc_mqtt_tls_client_config_free(struct altcp_tls_config *cfg)
{
    if (cfg != NULL) {
        altcp_tls_free_config(cfg);
    }
}

#endif /* SPLC_MQTT_TLS_CERT_ENABLE */