/**
 * @file splc_modbus_tcp_port.h
 * @brief Host glue: Modbus TCP server that carries the SimplePLC App link
 *        (SimplePLC.Studio over LAN / Wi-Fi) next to USB CDC / RS485.
 *
 * Design (SimplePLC handoff, decision 49):
 *   - SimplePLC owns no sockets. This file builds a modbus_transport_t with
 *     kind = MODBUS_TRANSPORT_KIND_TCP and registers it with
 *     plc_modbus_cfg_add_transport(); nanoMODBUS does the MBAP framing.
 *   - NO extra task and NO extra stack: the listening socket and the single
 *     client socket are non-blocking and are served from the transport
 *     callbacks, which SimplePLC calls from the engine task ("SPLC") only.
 *   - One client at a time (one App at a time, decision 49). A new connection
 *     REPLACES the old one, so an App that lost its cable and reconnects is
 *     never locked out by a half-open socket.
 *   - Own port (default 5020), because apps/modbus_tcp_server already owns 502
 *     (transparent bridge to the RS485 slaves). Nothing of that file is touched.
 *
 * SECURITY: Modbus TCP has no authentication. Anyone who can reach the port can
 * deploy rules, FACTORY_RESET, or change the MQTT broker password. Restrict it
 * with SPLC_HOST_MODBUS_TCP_ALLOW_NET/_MASK or leave the feature off.
 *
 * Needs SPLC_MODBUS_MAX_TRANSPORTS >= 2 in splcopts.h (one extra slot, about
 * 392 B RAM). Compiles to nothing when SPLC_HOST_MODBUS_TCP_ENABLE = 0.
 */
#ifndef SPLC_MODBUS_TCP_PORT_H
#define SPLC_MODBUS_TCP_PORT_H

#include <stdbool.h>
#include "splc_opt.h"   /* pulls in splcopts.h of this product */

/* ---- Options of THIS port (override in splcopts.h) ---------------------- */

#ifndef SPLC_HOST_MODBUS_TCP_ENABLE
#define SPLC_HOST_MODBUS_TCP_ENABLE      0
#endif

/* TCP port the App connects to. Must differ from EDGE_DEFAULT_MODBUS_TCP_PORT
 * (checked in the .c file). */
#ifndef SPLC_HOST_MODBUS_TCP_PORT
#define SPLC_HOST_MODBUS_TCP_PORT        5020
#endif

/* Optional source filter: a peer is accepted only when
 * (peer_ip & MASK) == (NET & MASK), both in host byte order,
 * e.g. NET 0xC0A80100UL + MASK 0xFFFFFF00UL = 192.168.1.0/24.
 * NET = 0 (default) accepts every peer. */
#ifndef SPLC_HOST_MODBUS_TCP_ALLOW_NET
#define SPLC_HOST_MODBUS_TCP_ALLOW_NET   0UL
#endif
#ifndef SPLC_HOST_MODBUS_TCP_ALLOW_MASK
#define SPLC_HOST_MODBUS_TCP_ALLOW_MASK  0xFFFFFFFFUL
#endif

/* Longest time one reply may wait for TCP send-buffer space. The engine task
 * is blocked meanwhile, so keep it small. */
#ifndef SPLC_HOST_MODBUS_TCP_TX_TIMEOUT_MS
#define SPLC_HOST_MODBUS_TCP_TX_TIMEOUT_MS 200U
#endif

#if SPLC_HOST_MODBUS_TCP_ENABLE && (SPLC_MODBUS_MAX_TRANSPORTS < 2)
#error "SPLC_HOST_MODBUS_TCP_ENABLE needs SPLC_MODBUS_MAX_TRANSPORTS >= 2 in splcopts.h"
#endif
#if (SPLC_HOST_MODBUS_TCP_PORT < 1) || (SPLC_HOST_MODBUS_TCP_PORT > 65535)
#error "SPLC_HOST_MODBUS_TCP_PORT must be 1..65535"
#endif

#if SPLC_HOST_MODBUS_TCP_ENABLE

#ifdef __cplusplus
extern "C" {
#endif

/* Registers the TCP transport with SimplePLC. Call ONCE, from the engine task,
 * AFTER plc_engine_init(). Returns false (and logs why) when SimplePLC rejects
 * it (no free slot). The socket itself is opened lazily by the transport pump,
 * as soon as the host has a network interface with an IP. */
bool splc_modbus_tcp_port_start(void);

#ifdef __cplusplus
}
#endif

#else

static inline bool splc_modbus_tcp_port_start(void) { return true; }

#endif /* SPLC_HOST_MODBUS_TCP_ENABLE */

#endif /* SPLC_MODBUS_TCP_PORT_H */