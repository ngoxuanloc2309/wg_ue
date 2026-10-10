/**
 * @file network_manager.h
 * @brief Multi-interface network manager with failover support
 */
#ifndef __NETWORK_MANAGER_H__
#define __NETWORK_MANAGER_H__

#include <stdint.h>
#include <stdbool.h>
#include "FreeRTOS.h"
#include "event_groups.h"

/* Forward declaration */
struct netif;

/*******************************************************************************
 * Network Interface Types
 ******************************************************************************/
typedef enum {
    NET_IF_ETH = 0,
    NET_IF_WIFI,
    NET_IF_LTE,
    NET_IF_USB,
    NET_IF_COUNT,        /* Total count for array sizes */
    NET_IF_NONE = 255    /* Special value: no interface selected */
} net_if_type_t;

/** Return the canonical human-readable name for an interface type. */
const char* network_interface_name(net_if_type_t if_type);

typedef void (*network_route_callback_t)(net_if_type_t old_interface,
                                         net_if_type_t new_interface,
                                         uint32_t generation,
                                         void *context);

/*******************************************************************************
 * Network States
 ******************************************************************************/
typedef enum {
    NET_STATE_DOWN = 0,
    NET_STATE_CONNECTING,
    NET_STATE_UP
} net_state_t;

/*******************************************************************************
 * Network Interface Status
 ******************************************************************************/
typedef enum {
    NET_IF_STATUS_DOWN = 0,      /* Link is down */
    NET_IF_STATUS_DEGRADED,       /* Link up but no IP */
    NET_IF_STATUS_UP             /* Link up + IP obtained */
} net_if_status_t;

/*******************************************************************************
 * Network Mode (DHCP or Static)
 ******************************************************************************/
typedef enum {
    NET_MODE_DHCP = 0,
    NET_MODE_STATIC
} net_mode_t;

/*******************************************************************************
 * Network Configuration Structures
 ******************************************************************************/

/* Static IP configuration */
typedef struct {
    uint32_t ip;            /* IP address (e.g., 192.168.1.100) */
    uint32_t netmask;       /* Subnet mask (e.g., 255.255.255.0) */
    uint32_t gateway;       /* Default gateway */
    uint32_t dns_primary;   /* Primary DNS server */
    uint32_t dns_secondary; /* Secondary DNS server */
} network_ip_config_t;

/* Per-interface configuration */
typedef struct {
    bool enabled;                   /* Enable/disable interface */
    bool dhcp;                      /* TRUE = DHCP, FALSE = Static */
    network_ip_config_t static_ip;  /* Used when dhcp = FALSE */
    uint8_t priority;              /* Lower = higher priority (0=highest) */
    uint8_t mac_addr[6];            /* MAC address */
    char ssid[33];                  /* WiFi SSID (max 32 + NULL) */
    char password[65];              /* WiFi Password (max 64 + NULL) */
    char apn[64];                   /* LTE APN (max 63 + NULL) */
    char username[33];              /* Username (e.g., PPPoS LTE) */
} network_interface_config_t;

/* Full network configuration */
typedef struct {
    network_interface_config_t eth;
    network_interface_config_t wifi;
    network_interface_config_t lte;
} network_config_t;

/*******************************************************************************
 * Network Event Bits
 ******************************************************************************/
#define LWIP_READY_BIT    (1 << 0)  /* lwIP initialization complete */
#define ETH_LINK_BIT      (1 << 1)  /* Ethernet link up */
#define WIFI_READY_BIT    (1 << 2)  /* WiFi ready */
#define LTE_READY_BIT     (1 << 3)  /* LTE ready */
#define USB_READY_BIT     (1 << 4)  /* USB network ready */

/* Interface status change bits */
#define ETH_STATUS_BIT    (1 << 8)
#define WIFI_STATUS_BIT   (1 << 9)
#define LTE_STATUS_BIT    (1 << 10)
#define NET_RELOAD_CONFIG_BIT (1 << 11)

#define ALL_NET_READY     (LWIP_READY_BIT | ETH_LINK_BIT | WIFI_READY_BIT | LTE_READY_BIT | USB_READY_BIT)

/*******************************************************************************
 * Network Manager APIs
 ******************************************************************************/

/**
 * @brief Initialize network manager
 */
void network_manager_init(void);

/**
 * @brief Load network configuration from storage
 * @return 0 on success, -1 on failure
 */
int network_manager_load_config(void);

/**
 * @brief Save network configuration to storage
 * @param config: Pointer to configuration structure
 * @return 0 on success, -1 on failure
 */
int network_manager_save_config(const network_config_t *config);

/**
 * @brief Get current network configuration
 * @param config: Pointer to receive configuration
 * @return 0 on success, -1 on failure
 */
int network_manager_get_config(network_config_t *config);

/**
 * @brief Network monitoring task
 * @param params: Task parameters
 */
void network_task(void *params);

/**
 * @brief Failover management task
 * @param params: Task parameters
 */
void network_failover_task(void *params);

/**
 * @brief Get the network events handle for synchronization
 * @return EventGroupHandle_t - handle for use with xEventGroupWaitBits
 */
EventGroupHandle_t network_get_event_group(void);

/**
 * @brief Wait for lwIP to be ready before initializing network interfaces
 * @note This should be called at the start of any network interface task
 *       before using lwIP functions
 */
void network_wait_lwip_ready(void);

/*******************************************************************************
 * Interface Management APIs
 ******************************************************************************/

/**
 * @brief Enable a network interface
 * @param if_type: Interface type (NET_IF_ETH, NET_IF_WIFI, etc.)
 * @return 0 on success, -1 on failure
 */
int network_interface_enable(net_if_type_t if_type);

/**
 * @brief Disable a network interface
 * @param if_type: Interface type
 * @return 0 on success, -1 on failure
 */
int network_interface_disable(net_if_type_t if_type);

/**
 * @brief Get interface status
 * @param if_type: Interface type
 * @return Interface status (NET_IF_STATUS_*)
 */
net_if_status_t network_interface_get_status(net_if_type_t if_type);

/**
 * @brief Get lwIP netif structure for an interface
 * @param if_type: Interface type
 * @return Pointer to struct netif, or NULL if not available
 */
struct netif* network_interface_get_netif(net_if_type_t if_type);

/**
 * @brief Set configuration for a specific interface
 * @param if_type: Interface type
 * @param config: Pointer to interface configuration
 * @return 0 on success, -1 on failure
 */
int network_interface_set_config(net_if_type_t if_type, const network_interface_config_t *config);
void network_manager_request_wifi_connect(void);

/**
 * @brief Get configuration for a specific interface
 * @param if_type: Interface type
 * @param config: Pointer to receive configuration
 * @return 0 on success, -1 on failure
 */
int network_interface_get_config(net_if_type_t if_type, network_interface_config_t *config);

/*******************************************************************************
 * Failover APIs
 ******************************************************************************/

/**
 * @brief Set primary (preferred) interface
 * @param if_type: Interface type to set as primary
 * @return 0 on success, -1 on failure
 */
int network_set_primary_interface(net_if_type_t if_type);

/**
 * @brief Get currently active interface (with default route)
 * @return Active interface type
 */
net_if_type_t network_get_active_interface(void);
uint32_t network_get_route_generation(void);
int network_manager_register_route_callback(network_route_callback_t callback,
                                            void *context);

/**
 * @brief Reachability-probe statistics for the active route
 */
typedef struct {
    uint32_t probe_ok;                 /* Successful probes */
    uint32_t probe_fail;               /* Failed probes */
    char suspected[16];                /* Demoted interface name or "" */
} network_probe_stats_t;

/**
 * @brief Get reachability probe statistics
 * @param out: Output statistics (may not be NULL)
 */
void network_get_probe_stats(network_probe_stats_t *out);


/**
 * @brief Force switch to specific interface
 * @param if_type: Target interface
 * @return 0 on success, -1 on failure
 */
int network_switch_to_interface(net_if_type_t if_type);

/*******************************************************************************
 * Status APIs
 ******************************************************************************/

/**
 * @brief Get network state for an interface
 * @param if_type: Interface type
 * @return Network state (NET_STATE_*)
 */
net_state_t network_get_state(net_if_type_t if_type);

/**
 * @brief Get IP address as string for an interface
 * @param if_type: Interface type
 * @return Static string with IP address, or "0.0.0.0" if unavailable
 */
const char* network_get_ip_address(net_if_type_t if_type);

/**
 * @brief Legacy API - Set priority
 * @deprecated Use network_set_primary_interface() instead
 */
int network_set_priority(net_if_type_t primary, net_if_type_t secondary);

#endif /* __NETWORK_MANAGER_H__ */
