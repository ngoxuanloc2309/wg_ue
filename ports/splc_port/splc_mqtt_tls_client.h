/**
 * @file splc_mqtt_tls_client.h
 * @brief Stage 2 (part 1): build the lwIP altcp_tls client configuration from the
 *        certificates stored by splc_mqtt_tls_port (littlefs certs/).
 *
 *   certs/ca.pem only                -> one-way TLS (server authentication)
 *   certs/ca.pem + client.crt + key  -> mutual TLS (2-way authentication)
 *   anything else                    -> NULL (the caller must NOT fall back to plain MQTT
 *                                       silently when the broker asked for TLS)
 *
 * Call splc_mqtt_tls_client_config_create() right before mqtt_client_connect() and put the
 * result in mqtt_connect_client_info_t.tls_config. The returned config is owned by lwIP
 * once a connection uses it; free it with splc_mqtt_tls_client_config_free() when the
 * client is torn down and no connection uses it any more.
 */
#ifndef SPLC_MQTT_TLS_CLIENT_H
#define SPLC_MQTT_TLS_CLIENT_H

#include <stdbool.h>
#include "splc_opt.h"

#if SPLC_MQTT_TLS_CERT_ENABLE

struct altcp_tls_config;

/* true when certs/ca.pem exists (minimum for TLS). Cheap: does not read the files. */
bool splc_mqtt_tls_client_available(void);

/* Read the stored PEM files and create the lwIP TLS config. NULL on any failure
 * (missing CA, client cert without key or the reverse, out of memory, parse error);
 * the reason is logged with tag SPLC_TLS_CLI. */
struct altcp_tls_config *splc_mqtt_tls_client_config_create(void);

void splc_mqtt_tls_client_config_free(struct altcp_tls_config *cfg);

#endif /* SPLC_MQTT_TLS_CERT_ENABLE */
#endif /* SPLC_MQTT_TLS_CLIENT_H */