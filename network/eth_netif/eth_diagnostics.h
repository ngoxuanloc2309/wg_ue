#ifndef ETH_DIAGNOSTICS_H
#define ETH_DIAGNOSTICS_H

#include <stdint.h>

typedef struct {
  uint32_t phy_address;
  uint32_t phy_id1;
  uint32_t phy_id2;
  uint32_t bmsr;
  int32_t link_state;
  uint32_t speed;
  uint32_t duplex;
  uint32_t rx_complete;
  uint32_t tx_complete;
  uint32_t dma_errors;
  uint32_t rx_buffer_unavailable;
  uint32_t phy_read_errors;
  uint32_t phy_write_errors;
  uint32_t link_transitions;
  uint32_t dhcp_tx;
  uint32_t dhcp_rx;
  uint32_t dhcp_discover;
  uint32_t dhcp_offer;
  uint32_t dhcp_request;
  uint32_t dhcp_ack;
  uint32_t dhcp_nack;
  uint32_t dhcp_last_xid;
  uint32_t dhcp_lwip_xid;
  uint8_t dhcp_state;
} ethernetif_diag_t;

#endif
