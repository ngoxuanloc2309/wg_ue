/**
 * @file ethernetif.h
 * @brief lwIP Ethernet interface header
 */
#ifndef __ETHERNETIF_H__
#define __ETHERNETIF_H__

#include "lwip/netif.h"
#include "lwip/timeouts.h"
#include "eth_diagnostics.h"

err_t ethernetif_init(struct netif *netif);
void ethernetif_set_mac(const uint8_t mac[6]);
void ethernetif_get_diag(ethernetif_diag_t *diag);
void ethernetif_get_dhcp_dns(uint32_t *primary, uint32_t *secondary);
uint32_t ethernetif_get_link_checkpoint(void);
uint32_t ethernetif_get_link_stack_hwm(void);
void ethernetif_input(void* argument);
void ethernetif_set_link(struct netif *netif, uint8_t link);

#endif /* __ETHERNETIF_H__ */
