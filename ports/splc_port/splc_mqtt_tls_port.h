/**
 * @file splc_mqtt_tls_port.h
 * @brief Host glue between SimplePLC's MQTT TLS certificate block (plc_mqtt_tls, ICD MQTT-003,
 *        0x0F00..0x0F43) and the gateway's littlefs.
 *
 * plc_mqtt_tls checks every 120-byte chunk the App sends and hands it over, in order, through
 * the functions registered here. They run in the engine task and must stay short, so each
 * chunk is one littlefs append (open + write + close, one metadata commit):
 *
 *   begin   remove the half-written temp file of this certificate type
 *   chunk   append the bytes to "<final path>.tmp"
 *   commit  check the byte count, then rename the temp file over the final one (littlefs
 *           rename is atomic: the old certificate stays valid until the new one is complete)
 *   abort   remove the temp file
 *   clear   remove all three certificates (FACTORY_RESET; CLEAR_RULES never calls it)
 *
 * Files (littlefs, relative paths like the "configs" directory of config_manager):
 *   certs/ca.pem        type 1  Root CA
 *   certs/client.crt    type 2  Client certificate
 *   certs/client.key    type 3  Client private key (stored as received, NOT encrypted)
 *
 * What this glue does NOT do (stage 2): it does not parse the certificates and the MQTT
 * client does not use them yet. splc_mqtt_port.c still ignores a broker block with
 * use_tls = 1 (the host keeps its own broker), so accepting certificates changes nothing
 * on the wire until the client does TLS.
 *
 * Compiles to nothing when SPLC_MQTT_TLS_CERT_ENABLE = 0.
 */
#ifndef SPLC_MQTT_TLS_PORT_H
#define SPLC_MQTT_TLS_PORT_H

#include <stdint.h>
#include "splc_opt.h"

#if SPLC_MQTT_TLS_CERT_ENABLE

/* Register the host functions with the library. Engine task, once, after plc_engine_init().
 * Logs and returns when registration fails. */
void splc_mqtt_tls_port_init(void);

/* littlefs path of the stored certificate of `type` (1 Root CA, 2 client cert, 3 client key);
 * NULL for any other type. For the TLS client (stage 2): file_io_read() it when connecting. */
const char *splc_mqtt_tls_port_path(uint8_t type);

#else
static inline void splc_mqtt_tls_port_init(void) {}
#endif

#endif /* SPLC_MQTT_TLS_PORT_H */