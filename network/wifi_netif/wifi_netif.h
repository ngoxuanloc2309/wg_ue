/**
 * @file wifi_netif.h
 * @brief lwIP netif adapter for ST67W611 (T02 architecture)
 *
 * Provides the bridge between lwIP and ST67W611 network traffic.
 * Handles TX/RX between lwIP and the optional ST67W611 lwIP adapter.
 */
#ifndef __WIFI_NETIF_H__
#define __WIFI_NETIF_H__

#include <stdint.h>
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "network_manager.h"

typedef struct {
    uint32_t tx_packets;
    uint32_t tx_errors;
    uint32_t rx_notify;
    uint32_t rx_packets;
    uint32_t rx_empty;
    uint32_t rx_input_errors;
    uint8_t dhcp_state;
    uint64_t spi_rx_packets;
    uint64_t spi_rx_drops;
    uint64_t spi_io_errors;
    uint64_t spi_header_errors;
    uint64_t spi_rx_stalls;
    uint32_t dhcp_tx;
    uint32_t dhcp_rx;
    uint32_t dhcp_discover;
    uint32_t dhcp_offer;
    uint32_t dhcp_request;
    uint32_t dhcp_ack;
    uint32_t dhcp_nack;       /**< DHCP NACK frames received (type 6) */
    uint32_t dhcp_last_xid;   /**< xid of last DHCP frame seen on wire */
    uint32_t dhcp_lwip_xid;   /**< xid currently used by lwIP DHCP client */
    uint32_t transport_rebinds;
    uint32_t transport_rebind_failures;
    uint8_t transport_bound;
} wifi_netif_diag_t;

/**
 * @brief Initialize Wi-Fi netif adapter
 * @return 0 on success, -1 on failure
 */
int32_t wifi_netif_init(void);

/**
 * @brief Register the lwIP netif pointer used for RX processing
 * @param nif Pointer to the wifi netif
 */
void wifi_netif_set_netif(struct netif *nif);
void wifi_netif_set_status_callback(void);

/** Apply the configured DHCP/static IPv4 settings to the host netif. */
int32_t wifi_netif_apply_config(const network_config_t *config);

/** Return non-zero when the Wi-Fi netif has a non-zero IPv4 address. */
int wifi_netif_has_ip(void);
int wifi_netif_restart_dhcp(void);
void wifi_netif_get_dns(uint32_t *primary, uint32_t *secondary);
void wifi_netif_request_rx_flush(void);
int wifi_netif_wait_rx_flush(uint32_t timeout_ms);
void wifi_netif_get_diag(wifi_netif_diag_t *diag);
void wifi_netif_dump_spi_queues(void);
void wifi_netif_transport_invalidated(void);
int32_t wifi_netif_rebind_after_recovery(void);
int wifi_netif_is_bound(void);

/**
 * @brief Output callback registered as netif->linkoutput
 * Called by lwIP to send a packet out the Wi-Fi interface.
 * @return ERR_OK on success, error code on failure
 */
err_t wifi_netif_output(struct netif *netif, struct pbuf *p);

#endif /* __WIFI_NETIF_H__ */
