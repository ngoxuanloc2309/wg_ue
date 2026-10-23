/**
 * @file splc_wifi_port.h
 * @brief Host glue between SimplePLC's Wi-Fi block (plc_wifi, ICD WIFI-004, 0x1300..0x13FF)
 *        and the gateway's Wi-Fi stack (network_manager + the ST67 driver).
 *
 * plc_wifi owns no radio and no Flash: it keeps what the App wrote and calls the NON-BLOCKING
 * functions registered here. Everything slow (the ST67 scan, config_save() to the littlefs
 * file, applying a static IP) runs in a small worker task ("SPLC_WIFI"); the callbacks only
 * set a flag or copy a cached value and return at once, in the engine task.
 *
 * What the host stack can and cannot report (checked in the code, not assumed):
 *   - connection state: wifi_driver_get_state(); AUTH_FAILED / SSID_NOT_FOUND are NOT
 *     distinguished by the driver (a failed connect is DISCONNECTED/ERROR, error_code = driver);
 *   - IP / MAC: from the host Wi-Fi lwIP netif; RSSI: the ST67 wrapper has no RSSI
 *     (st67w611_get_rssi() = UNSUPPORTED), so it is reported as 0 dBm while connected;
 *   - scan results: ssid / rssi / channel / bssid; the driver gives no security mode, so
 *     auth_mode is reported as 0.
 *   - persistence: the host's own config.json ("network" node, config_save()). The password
 *     is stored there in clear text, like the host's own web/shell configuration.
 *
 * All of it compiles to nothing when SPLC_WIFI_ENABLE = 0.
 */
#ifndef SPLC_WIFI_PORT_H
#define SPLC_WIFI_PORT_H

#include "splc_opt.h"

#ifndef SPLC_HOST_WIFI_TASK_STACK_WORDS
#define SPLC_HOST_WIFI_TASK_STACK_WORDS 1280U
#endif
#ifndef SPLC_HOST_WIFI_TASK_PRIO
#define SPLC_HOST_WIFI_TASK_PRIO        5U     /* below the engine task (6) */
#endif

#if SPLC_WIFI_ENABLE
/* Create the worker task and register the host functions with the library.
 * Engine task, once, after plc_engine_init(). Logs and returns when registration fails. */
void splc_wifi_port_init(void);
#else
static inline void splc_wifi_port_init(void) {}
#endif

#endif /* SPLC_WIFI_PORT_H */