/**
 * @file wifi_driver.h
 * @brief WiFi (ST67W611) driver for Mission T01/T02
 */
#ifndef __WIFI_DRIVER_H__
#define __WIFI_DRIVER_H__

#include <stdint.h>
#if (ST67_ARCH == 2)
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#else
struct netif;
#endif

typedef void (*wifi_event_cb_t)(uint8_t event);

/* WiFi connection state */
typedef enum {
    WIFI_STATE_IDLE = 0,
    WIFI_STATE_INITIALIZING,
    WIFI_STATE_INIT,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_GOT_IP,
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_ERROR
} wifi_state_t;

/**
 * @brief Initialize the ST67W6X Wi-Fi stack.
 * @return T02 host netif, or NULL on failure. T01 has no host netif and
 *         reports success through wifi_driver_get_state().
 */
struct netif* wifi_driver_init(void);

/**
 * @brief Start WiFi with DHCP
 * @return 0 on success, -1 on failure
 */
int wifi_driver_start(void);

/**
 * @brief Stop WiFi
 * @return 0 on success, -1 on failure
 */
int wifi_driver_stop(void);

/**
 * @brief Connect to WiFi network
 * @param ssid: SSID
 * @param pass: Password
 * @return 0 on success, -1 on failure
 */
int wifi_driver_connect(const char *ssid, const char *pass);

/** Recover the ST67 module from an error state in task context. */
int wifi_driver_recover(void);

/** Get the underlying ST67 module state for diagnostics. */
int wifi_driver_get_st67_state(int *state);

#include <stdbool.h>

/** T01 host-control helpers. */
int wifi_driver_scan(void);
bool wifi_driver_is_scanning(void);
const char* wifi_driver_get_scan_results_json(void);
int wifi_driver_get_ip(uint8_t ip[4], uint8_t gateway[4], uint8_t netmask[4]);
int wifi_driver_ping(const char *location);
uint32_t wifi_driver_vendor_log_dropped(void);

/**
 * @brief Disconnect from WiFi
 * @return 0 on success, -1 on failure
 */
int wifi_driver_disconnect(void);

/**
 * @brief Send data
 * @param data: Pointer to data
 * @param len: Data length
 * @return Number of bytes sent, or -1 on error
 */
int wifi_driver_send(const uint8_t *data, uint16_t len);

/**
 * @brief Receive data
 * @param data: Buffer to receive
 * @param max_len: Maximum bytes to receive
 * @return Number of bytes received, or -1 on error
 */
int wifi_driver_recv(uint8_t *data, uint16_t max_len);

/**
 * @brief Get connection status
 * @return 1 if connected, 0 if disconnected
 */
uint8_t wifi_driver_status(void);

/**
 * @brief Register event callback
 * @param cb: Callback function
 */
void wifi_driver_set_event_cb(wifi_event_cb_t cb);

/** Notify the driver that the host netif has completed DHCP. */
void wifi_driver_notify_netif_got_ip(void);

/**
 * @brief Get signal strength
 * @return RSSI value (negative dBm) or 0 if not connected
 */
int8_t wifi_driver_get_rssi(void);

/**
 * @brief Get WiFi state
 * @return Current WiFi state
 */
wifi_state_t wifi_driver_get_state(void);
bool wifi_driver_is_connect_in_progress(void);

/**
 * @brief Get the Wi-Fi netif after the ST67 task has initialized it.
 */
struct netif *wifi_driver_get_netif(void);

/**
 * @brief Create the ST reference-style ST67 initialization task.
 * @return 0 on success, -1 on failure
 */
int wifi_driver_startup_task_create(void);

/**
 * @brief Create WiFi monitoring task
 * @return 0 on success, -1 on failure
 */
int wifi_driver_start_task(void);

#if (ST67_ARCH == 2)
/** T02-only host lwIP adapter API. */
int32_t wifi_netif_init(void);
void wifi_netif_set_netif(struct netif *nif);
err_t wifi_netif_output(struct netif *netif, struct pbuf *p);
#endif

#endif /* __WIFI_DRIVER_H__ */
