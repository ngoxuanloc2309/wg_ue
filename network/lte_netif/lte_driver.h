/**
 * @file lte_driver.h
 * @brief LTE PPPoS (A7680C) Driver Header - returns netif for network_manager
 */
#ifndef __LTE_DRIVER_H__
#define __LTE_DRIVER_H__

#include <stdint.h>
#include "lwip/netif.h"

typedef void (*lte_event_cb_t)(uint8_t event);

typedef enum {
    LTE_STATUS_DISABLED = 0,
    LTE_STATUS_IDLE,
    LTE_STATUS_SEARCHING,
    LTE_STATUS_REGISTERED,
    LTE_STATUS_CONNECTING,
    LTE_STATUS_CONNECTED,
    LTE_STATUS_DEGRADED,
    LTE_STATUS_RECOVERING,
    LTE_STATUS_ERROR
} lte_status_t;

typedef struct {
    lte_status_t status;
    int registration_state;
    int csq;
    int ber;
    int packet_attached;
    char operator_id[32];
    uint32_t registration_retries;
    uint32_t ppp_retries;
    uint32_t hard_resets;
    uint32_t uart_errors;
    int last_error;
} lte_diag_t;

/**
 * @brief Initialize LTE driver and register netif
 * @return Pointer to struct netif, or NULL on failure
 */
struct netif* lte_driver_init(void);

/**
 * @brief Start LTE
 * @return 0 on success, -1 on failure
 */
int lte_driver_start(void);

/**
 * @brief Stop LTE
 * @return 0 on success, -1 on failure
 */
int lte_driver_stop(void);

/**
 * @brief Initialize PPPoS driver
 * @param cb: Event callback
 */
void lte_driver_init_callback(lte_event_cb_t cb);

/**
 * @brief Connect to LTE network
 * @return 0 on success, -1 on failure
 */
int lte_driver_connect(void);

/**
 * @brief Disconnect from LTE
 * @return 0 on success, -1 on failure
 */
int lte_driver_disconnect(void);

/**
 * @brief Send data over PPP
 * @param data: Pointer to data
 * @param len: Data length
 * @return Number of bytes sent, or -1 on error
 */
int lte_driver_send(const uint8_t *data, uint16_t len);

/**
 * @brief Receive data over PPP
 * @param data: Buffer to receive
 * @param max_len: Maximum bytes to receive
 * @return Number of bytes received, or -1 on error
 */
int lte_driver_recv(uint8_t *data, uint16_t max_len);

/**
 * @brief Get connection status
 * @return 1 if connected, 0 if disconnected
 */
uint8_t lte_driver_status(void);

/**
 * @brief Set APN for LTE connection
 * @param apn: APN string
 */
void lte_driver_set_apn(const char *apn);
void lte_driver_set_credentials(const char *apn, const char *username,
                                const char *password);
lte_status_t lte_driver_get_status(void);
void lte_driver_get_diag(lte_diag_t *diag);
void lte_driver_get_dns(uint32_t *primary, uint32_t *secondary);

/**
 * @brief Get signal quality
 * @return CSQ value (RSSI*10 + ber) or 0 if not connected
 */
int lte_driver_get_csq(void);

/**
 * @brief Get operator name
 * @return Operator name string
 */
const char* lte_driver_get_operator(void);

#endif /* __LTE_DRIVER_H__ */
