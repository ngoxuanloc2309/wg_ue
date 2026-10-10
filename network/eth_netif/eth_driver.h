/**
 * @file eth_driver.h
 * @brief Ethernet (DP83848) driver - returns netif for network_manager
 */
#ifndef __ETH_DRIVER_H__
#define __ETH_DRIVER_H__

#include <stdint.h>
#include "lwip/netif.h"
#include "eth_diagnostics.h"

/**
 * @brief Initialize Ethernet driver and register netif
 * @return Pointer to struct netif, or NULL on failure
 */
struct netif* eth_driver_init(void);

/**
 * @brief Start Ethernet (after init)
 * @return 0 on success, -1 on failure
 */
int eth_driver_start(void);

/**
 * @brief Stop Ethernet
 * @return 0 on success, -1 on failure
 */
int eth_driver_stop(void);

/**
 * @brief Send Ethernet frame
 * @param data: Pointer to data
 * @param len: Data length
 * @return Number of bytes sent, or -1 on error
 */
int eth_driver_send(const uint8_t *data, uint16_t len);

/**
 * @brief Receive Ethernet frame
 * @param data: Buffer to receive data
 * @param max_len: Maximum bytes to receive
 * @return Number of bytes received, or -1 on error
 */
int eth_driver_recv(uint8_t *data, uint16_t max_len);

/**
 * @brief Get link status
 * @return 1 if link up, 0 if down
 */
uint8_t eth_driver_link_status(void);

/**
 * @brief Get MAC address
 * @param mac: Buffer to store MAC address (6 bytes)
 */
void eth_driver_get_mac(uint8_t *mac);
struct netif *eth_driver_get_netif(void);
void eth_driver_get_diag(ethernetif_diag_t *diag);
void eth_driver_get_dns(uint32_t *primary, uint32_t *secondary);

#endif /* __ETH_DRIVER_H__ */
