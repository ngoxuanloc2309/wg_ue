/**
 * @file eth_driver.c
 * @brief Ethernet (DP83848) driver - returns netif for network_manager
 */
#include "eth_driver.h"
#include "logger.h"
#include "lwip/netif.h"
#include "lwip/ip_addr.h"
#include "lwip/dhcp.h"
#include "lwip/tcpip.h"
#include "ethernetif.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "task.h"
#include "app_config.h"
#include <string.h>

static const char *TAG = "eth_driver";

#define ETH_LINK_TASK_STACK_SIZE_BYTES 2048U
#define ETH_LINK_TASK_STACK_DEPTH \
    (ETH_LINK_TASK_STACK_SIZE_BYTES / sizeof(StackType_t))

static struct netif eth_netif;
static uint8_t eth_mac_addr[6] = {0};
static uint32_t eth_dns_primary;
static uint32_t eth_dns_secondary;
/* Keep the link monitor outside heap_4.  Besides avoiding heap fragmentation,
 * this makes a genuine EthLink overflow distinguishable from an unrelated
 * heap writer corrupting a dynamically allocated task stack. */
static StaticTask_t eth_link_task_control;
static StackType_t eth_link_task_stack[ETH_LINK_TASK_STACK_DEPTH]
    __attribute__((aligned(8)));
static TaskHandle_t eth_link_task_handle;

typedef struct {
    SemaphoreHandle_t done;
    bool netif_created;
} eth_init_context_t;

/* Ethernet initialization is performed once. Keeping the callback context
 * static makes a late tcpip_callback safe even when the caller timed out. */
static eth_init_context_t eth_init_context;

/* External function from lwip_port/ethernetif.c */
extern void ethernet_link_thread(void* argument);

static void eth_status_callback(struct netif *netif)
{
    if (netif == NULL || netif_ip4_addr(netif)->addr == 0U) {
        eth_dns_primary = 0U;
        eth_dns_secondary = 0U;
        return;
    }

    if (dhcp_supplied_address(netif)) {
        char primary_text[IP4ADDR_STRLEN_MAX];
        char secondary_text[IP4ADDR_STRLEN_MAX];
        ethernetif_get_dhcp_dns(&eth_dns_primary, &eth_dns_secondary);
        if (eth_dns_primary == 0U) return;
        ip4addr_ntoa_r((const ip4_addr_t *)&eth_dns_primary,
                       primary_text, sizeof(primary_text));
        ip4addr_ntoa_r((const ip4_addr_t *)&eth_dns_secondary,
                       secondary_text, sizeof(secondary_text));
        LOGI(TAG, "ETH DHCP DNS captured: primary=%s secondary=%s",
             primary_text, secondary_text);
    }
}

/**
 * @brief Generate MAC address from STM32 unique ID
 */
static void generate_mac_address(uint8_t *mac) {
    /* Use STM32 unique ID for MAC generation */
    uint32_t* unique_id = (uint32_t*)0x080036F8;  /* STM32H5 unique ID */

    mac[0] = 0x00;  /* Locally administered */
    mac[1] = 0x80;
    mac[2] = 0xE1;
    mac[3] = (uint8_t)(*unique_id >> 16);
    mac[4] = (uint8_t)(*unique_id >> 8);
    mac[5] = (uint8_t)(*unique_id);
}

static void eth_driver_init_cb(void *ctx) {
    eth_init_context_t *init = (eth_init_context_t *)ctx;
    
    ip4_addr_t ipaddr, netmask, gw;
    /* Default: DHCP mode */
    ipaddr.addr = 0;
    netmask.addr = 0;
    gw.addr = 0;

    /* Add Ethernet interface inside tcpip thread */
    struct netif* netif_ptr = netif_add(&eth_netif,
                                         &ipaddr,
                                         &netmask,
                                         &gw,
                                         NULL,
                                         ethernetif_init,
                                         tcpip_input);

    init->netif_created = (netif_ptr != NULL);
    if (init->netif_created) {
#if LWIP_NETIF_HOSTNAME
        netif_set_hostname(netif_ptr, "GatewayV2-ETH");
#endif

        /* ethernetif owns the hardware/link state.  Keep the netif down
         * until the PHY/link thread confirms a physical link. */
        eth_netif.hwaddr_len = 6;
        memcpy(eth_netif.hwaddr, eth_mac_addr, 6);
        netif_set_down(&eth_netif);
        netif_set_link_down(&eth_netif);
        netif_set_status_callback(&eth_netif, eth_status_callback);
    }
    
    xSemaphoreGive(init->done);
}

/**
 * @brief Initialize Ethernet driver and register netif
 * @return Pointer to struct netif, or NULL on failure
 */
struct netif* eth_driver_init(void) {
    /* Generate MAC address */
    generate_mac_address(eth_mac_addr);
    ethernetif_set_mac(eth_mac_addr);

    if (eth_init_context.done == NULL) {
        eth_init_context.done = xSemaphoreCreateBinary();
    }
    if (eth_init_context.done == NULL) {
        LOGE(TAG, "Failed to create sync sem");
        return NULL;
    }
    eth_init_context.netif_created = false;
    (void)xSemaphoreTake(eth_init_context.done, 0);

    LOGI(TAG, "ETH init: before tcpip_callback");
    err_t callback_err = tcpip_callback(eth_driver_init_cb, &eth_init_context);
    if (callback_err != ERR_OK) {
        LOGE(TAG, "ETH init: tcpip_callback failed: %d", callback_err);
        return NULL;
    }

    LOGI(TAG, "ETH init: waiting for tcpip callback");
    if (xSemaphoreTake(eth_init_context.done, pdMS_TO_TICKS(3000)) != pdTRUE) {
        LOGE(TAG, "ETH init: timeout waiting for tcpip callback");
        return NULL;
    }
    LOGI(TAG, "ETH init: tcpip callback completed");

    if (!eth_init_context.netif_created || eth_netif.hwaddr_len == 0) {
        LOGE(TAG, "Failed to add Ethernet interface");
        return NULL;
    }

    /* Use the native static API deliberately: CMSIS stack_size is expressed
     * in bytes while FreeRTOS expects StackType_t units.  A static stack also
     * prevents an adjacent heap allocation from being misdiagnosed as an
     * EthLink stack overflow. */
    eth_link_task_handle = xTaskCreateStatic(
        ethernet_link_thread,
        "EthLink",
        ETH_LINK_TASK_STACK_DEPTH,
        &eth_netif,
        GW_TASK_PRIO_ETH_LINK,
        eth_link_task_stack,
        &eth_link_task_control);
    if (eth_link_task_handle == NULL) {
        LOGE(TAG, "Failed to create EthLink task");
        return NULL;
    }

    LOGI(TAG, "ETH driver initialized, MAC: %02X:%02X:%02X:%02X:%02X:%02X",
         eth_mac_addr[0], eth_mac_addr[1], eth_mac_addr[2],
         eth_mac_addr[3], eth_mac_addr[4], eth_mac_addr[5]);

    return &eth_netif;
}

/**
 * @brief Start Ethernet with DHCP
 */
int eth_driver_start(void) {
    if (!netif_is_up(&eth_netif) || !netif_is_link_up(&eth_netif)) {
        LOGW(TAG, "Cannot start Ethernet: link is down");
        return -1;
    }
    LOGI(TAG, "Ethernet interface started");
    return 0;
}

/**
 * @brief Stop Ethernet
 */
int eth_driver_stop(void) {
    dhcp_stop(&eth_netif);
    netif_set_link_down(&eth_netif);
    netif_set_down(&eth_netif);
    return 0;
}

/**
 * @brief Send Ethernet frame
 */
int eth_driver_send(const uint8_t *data, uint16_t len) {
    (void)data;
    (void)len;
    /* Not used - using lwIP directly */
    return -1;
}

/**
 * @brief Receive Ethernet frame
 */
int eth_driver_recv(uint8_t *data, uint16_t max_len) {
    (void)data;
    (void)max_len;
    /* Not used - using lwIP directly */
    return -1;
}

/**
 * @brief Get link status
 */
uint8_t eth_driver_link_status(void) {
    return netif_is_link_up(&eth_netif) ? 1 : 0;
}

/**
 * @brief Get MAC address
 */
void eth_driver_get_mac(uint8_t *mac) {
    memcpy(mac, eth_mac_addr, 6);
}

struct netif *eth_driver_get_netif(void) { return &eth_netif; }

void eth_driver_get_diag(ethernetif_diag_t *diag) { ethernetif_get_diag(diag); }

void eth_driver_get_dns(uint32_t *primary, uint32_t *secondary)
{
    if (primary != NULL) *primary = eth_dns_primary;
    if (secondary != NULL) *secondary = eth_dns_secondary;
}
