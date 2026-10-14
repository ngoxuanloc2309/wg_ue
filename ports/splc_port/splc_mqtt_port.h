/**
 * @file splc_mqtt_port.h
 * @brief Host glue between SimplePLC's MQTT Publish (plc_mqtt) and the gateway's
 *        MQTT client (apps/mqtt_client). SimplePLC decides WHEN to publish and
 *        renders the payload; this file only moves bytes and state.
 *
 * Context rule (SimplePLC decision 31): every splc_mqtt_*() call happens in the
 * engine task (task "SPLC"). Both functions below are called from there.
 * All of it compiles to nothing when SPLC_MQTT_ENABLE = 0.
 */
#ifndef SPLC_MQTT_PORT_H
#define SPLC_MQTT_PORT_H

#include <stdint.h>
#include "splc_opt.h"   /* pulls in splcopts.h of this product */

#if SPLC_MQTT_ENABLE

/* Registers the publish callback and the {device} fallback name.
 * Call in the engine task BEFORE plc_engine_init(). */
void splc_mqtt_port_init(void);

/* Pumps status host -> library and broker config library -> host client, and
 * reconnects when the connection was built from an older broker config.
 * Cheap: does real work at most every 100 ms. Call every engine-task loop. */
void splc_mqtt_port_poll(uint32_t now_ms);

#else

static inline void splc_mqtt_port_init(void) {}
static inline void splc_mqtt_port_poll(uint32_t now_ms) { (void)now_ms; }

#endif /* SPLC_MQTT_ENABLE */

#endif /* SPLC_MQTT_PORT_H */