/**
 * @file network_manager.c
 * @brief Multi-interface network manager with failover support
 */
#include "network_manager.h"
#include "logger.h"
#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os2.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/netifapi.h"
#include "lwip/tcpip.h"
#include "lwip/dhcp.h"
#include "lwip/dns.h"
#include "lwip/ip4_addr.h"
#include "lwip/inet.h"
#include "file_io.h"
#include "config_manager.h"
#include "wifi_driver.h"
#if (ST67_ARCH == 2)
#include "wifi_netif.h"
#endif
#include "lte_driver.h"
#include "ethernetif.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "cJSON.h"
#include "semphr.h"

/*******************************************************************************
 * Private Variables
 ******************************************************************************/

static const char *TAG = "netmgr";

/* Network events and config mutex */
static EventGroupHandle_t network_events = NULL;
static SemaphoreHandle_t config_mutex = NULL;

/* Array of netif pointers for each interface */
static struct netif* g_netif_array[NET_IF_COUNT] = { NULL };

/* Interface status tracking */
static volatile net_if_status_t g_interface_status[NET_IF_COUNT] = {
    NET_IF_STATUS_DOWN
};

/* Current active interface (default gateway) */
static volatile net_if_type_t g_active_interface = NET_IF_NONE;
static uint32_t g_route_generation = 0U;
static network_route_callback_t g_route_callback = NULL;
static void *g_route_callback_context = NULL;
static bool g_eth_link_confirmed = false;
static volatile bool g_wifi_manual_connect_requested = false;

/* Active-route reachability probe.  An interface with a valid lease is not
 * guaranteed to reach the internet (dead WAN, captive portal, LTE attached
 * without data).  A periodic async DNS probe through the current default
 * route detects dead ends; repeated failures demote the active interface so
 * find_best_interface() stops electing it until the suspicion expires. */
#define PROBE_PERIOD_MS        20000U
#define PROBE_FAIL_THRESHOLD   2U
#define PROBE_TIMEOUT_MS       10000U
/* Freshly bound paths get a grace window before probing: guest APs and some
 * mesh nodes drop or rate-limit DNS for a short period right after an
 * association, which would otherwise read as a dead uplink. */
#define PROBE_GRACE_MS         15000U
#define PROBE_SUSPECT_TTL_MS   120000U
/* Repeatedly-demoted paths back off exponentially so a flapping route does
 * not churn the default gateway every couple of minutes: 120s -> 240s ->
 * 480s -> 960s, capped here. */
#define PROBE_SUSPECT_TTL_MAX_MS 900000U
static const char PROBE_FALLBACK_HOST[] = "pool.ntp.org";

static bool s_probe_suspect[NET_IF_COUNT] = { false };
static TickType_t s_probe_suspect_at[NET_IF_COUNT];
static uint32_t s_probe_demotes[NET_IF_COUNT] = {0};
static TickType_t s_probe_suspect_ttl[NET_IF_COUNT];
static uint32_t s_probe_ok_count = 0U;
static uint32_t s_probe_fail_count = 0U;
static uint32_t s_probe_fail_streak = 0U;
static TickType_t s_probe_next_at = 0;

static volatile uint32_t s_probe_seq = 0U;
static volatile int32_t s_probe_result = -1; /* <0 none/pending, 0 ok, 1 fail */
static volatile bool s_probe_in_flight = false;
static TickType_t s_probe_sent_at = 0;
/* Must outlive the request: lwIP fills it asynchronously from tcpip ctx. */
static ip_addr_t s_probe_addr;

extern void eth_driver_get_dns(uint32_t *primary, uint32_t *secondary);

/* Defined with defaults below; declared here because route DNS application
 * runs before the configuration definition in this translation unit. */
static network_config_t g_network_config;
static network_config_t s_staging_network_config;
static bool s_network_changed = false;

void network_manager_request_wifi_connect(void)
{
    g_wifi_manual_connect_requested = true;
}

static void apply_route_dns_cb(void *arg)
{
    net_if_type_t route = (net_if_type_t)(uintptr_t)arg;
    ip_addr_t primary;
    ip_addr_t secondary;
    uint32_t lte_primary = 0U;
    uint32_t lte_secondary = 0U;
    uint32_t wifi_primary = 0U;
    uint32_t wifi_secondary = 0U;
    uint32_t eth_primary = 0U;
    uint32_t eth_secondary = 0U;
    char primary_str[IP4ADDR_STRLEN_MAX];
    char secondary_str[IP4ADDR_STRLEN_MAX];

    /* DNS is global in lwIP. Always build both servers from the selected
     * route; never inherit the servers installed by the previous route. */
    IP4_ADDR(ip_2_ip4(&primary), 1, 1, 1, 1);
    IP4_ADDR(ip_2_ip4(&secondary), 8, 8, 8, 8);

    if (route == NET_IF_LTE) {
        lte_driver_get_dns(&lte_primary, &lte_secondary);
        if (lte_primary != 0U) {
            ip_2_ip4(&primary)->addr = lte_primary;
            ip_2_ip4(&secondary)->addr = lte_secondary;
        } else {
            IP4_ADDR(ip_2_ip4(&primary), 1, 1, 1, 1);
            IP4_ADDR(ip_2_ip4(&secondary), 8, 8, 8, 8);
            LOGW(TAG, "LTE has no peer DNS; using fallback 1.1.1.1/8.8.8.8");
        }
        dns_setserver(0, &primary);
        dns_setserver(1, &secondary);
    } else if (route == NET_IF_WIFI) {
        wifi_netif_get_dns(&wifi_primary, &wifi_secondary);
        if (wifi_primary != 0U) {
            ip_2_ip4(&primary)->addr = wifi_primary;
            ip_2_ip4(&secondary)->addr = wifi_secondary;
        } else {
            IP4_ADDR(ip_2_ip4(&primary), 1, 1, 1, 1);
            IP4_ADDR(ip_2_ip4(&secondary), 8, 8, 8, 8);
            LOGW(TAG, "WiFi has no DHCP DNS; using fallback 1.1.1.1/8.8.8.8");
        }
        dns_setserver(0, &primary);
        dns_setserver(1, &secondary);
    } else if (route == NET_IF_ETH) {
        const network_ip_config_t *eth_ip = &g_network_config.eth.static_ip;
        eth_driver_get_dns(&eth_primary, &eth_secondary);
        if (g_network_config.eth.dhcp && eth_primary != 0U) {
            ip_2_ip4(&primary)->addr = eth_primary;
            ip_2_ip4(&secondary)->addr = eth_secondary != 0U
                                       ? eth_secondary
                                       : eth_primary;
        } else if (!g_network_config.eth.dhcp && eth_ip->dns_primary != 0U) {
            ip_2_ip4(&primary)->addr = eth_ip->dns_primary;
            ip_2_ip4(&secondary)->addr = eth_ip->dns_secondary != 0U
                                       ? eth_ip->dns_secondary
                                       : eth_ip->dns_primary;
        } else {
            LOGW(TAG, "ETH lease has no DNS; using fallback 1.1.1.1/8.8.8.8");
        }
        dns_setserver(0, &primary);
        dns_setserver(1, &secondary);
    }
    ip4addr_ntoa_r(ip_2_ip4(&primary), primary_str, sizeof(primary_str));
    ip4addr_ntoa_r(ip_2_ip4(&secondary), secondary_str, sizeof(secondary_str));
    LOGI(TAG, "DNS servers applied: route=%s primary=%s secondary=%s",
         network_interface_name(route), primary_str, secondary_str);
}

const char* network_interface_name(net_if_type_t if_type) {
    switch (if_type) {
        case NET_IF_ETH:  return "ETH";
        case NET_IF_WIFI: return "WIFI";
        case NET_IF_LTE:  return "LTE";
        case NET_IF_USB:  return "USB";
        default:          return "NONE";
    }
}

/* Network configuration (default values) */
static network_config_t g_network_config = {
    .eth = {
        .enabled = true,
        .dhcp = true,
        .priority = 0,  /* Highest priority */
        .static_ip = { 0 }
    },
    .wifi = {
        .enabled = false,
        .dhcp = true,
        .priority = 1,
        .static_ip = { 0 }
    },
    .lte = {
        .enabled = false,
        .dhcp = true,
        .priority = 2,
        .static_ip = { 0 }
    }
};

/* External functions from drivers */
extern struct netif* eth_driver_init(void);

/*******************************************************************************
 * lwIP Initialization Callback
 ******************************************************************************/

/**
 * @brief lwIP initialization callback
 * @note Called by tcpip_init when lwIP core is fully initialized
 */
static void lwip_init_done(void *arg) {
    (void)arg;
    if (network_events != NULL) {
        xEventGroupSetBits(network_events, LWIP_READY_BIT);
        LOGI(TAG, "lwIP initialized successfully");
    }
}

/*******************************************************************************
 * Public APIs
 ******************************************************************************/

EventGroupHandle_t network_get_event_group(void) {
    return network_events;
}

void network_wait_lwip_ready(void) {
    if (network_events == NULL) {
        LOGE(TAG, "Network events not initialized!");
        return;
    }

    EventBits_t bits = xEventGroupWaitBits(
        network_events,
        LWIP_READY_BIT,
        pdFALSE,             /* DO NOT clear the bit before returning */
        pdTRUE,              /* Wait for ALL bits */
        portMAX_DELAY
    );

    if (bits & LWIP_READY_BIT) {
        LOGD(TAG, "lwIP is ready, proceeding...");
    }
}

/**
 * @brief Register a network interface with the manager
 */
int network_register_interface(net_if_type_t if_type, struct netif* netif) {
    if (if_type >= NET_IF_COUNT || netif == NULL) {
        return -1;
    }

    g_netif_array[if_type] = netif;
    LOGI(TAG, "Registered interface %d", if_type);

    return 0;
}

/**
 * @brief Update interface status based on link and IP
 */
static void update_interface_status(net_if_type_t if_type) {
    if (if_type >= NET_IF_COUNT || g_netif_array[if_type] == NULL) {
        return;
    }

    struct netif* netif = g_netif_array[if_type];
    net_if_status_t old_status = g_interface_status[if_type];
    bool wifi_dhcp_bound = true;

#if (ST67_ARCH == 2)
    if (if_type == NET_IF_WIFI && g_network_config.wifi.dhcp) {
        wifi_netif_diag_t diag;
        wifi_netif_get_diag(&diag);
        wifi_dhcp_bound = (diag.dhcp_state == 10U);
    }
#endif

    if (if_type == NET_IF_ETH && !g_eth_link_confirmed) {
        g_interface_status[if_type] = NET_IF_STATUS_DOWN;
    } else if (!netif_is_link_up(netif)) {
        g_interface_status[if_type] = NET_IF_STATUS_DOWN;
    } else if (netif_ip4_addr(netif)->addr == 0 || !wifi_dhcp_bound) {
        g_interface_status[if_type] = NET_IF_STATUS_DEGRADED;
    } else {
        g_interface_status[if_type] = NET_IF_STATUS_UP;
    }

    /* Log status change */
    if (old_status != g_interface_status[if_type]) {
        const char* status_str[] = { "DOWN", "DEGRADED", "UP" };
        LOGI(TAG, "Interface %s(%d) status: %s -> %s",
             network_interface_name(if_type), if_type,
             status_str[old_status], status_str[g_interface_status[if_type]]);

        /* Set event bit for status change */
        if (if_type == NET_IF_ETH) {
            xEventGroupSetBits(network_events, ETH_STATUS_BIT);
        } else if (if_type == NET_IF_WIFI) {
            xEventGroupSetBits(network_events, WIFI_STATUS_BIT);
        } else if (if_type == NET_IF_LTE) {
            xEventGroupSetBits(network_events, LTE_STATUS_BIT);
        }
    }
}

/* Called from the tcpip thread.  A stale seq means a late reply to an
 * abandoned request; drop it instead of corrupting the current result. */
static void network_probe_dns_cb(const char *name, const ip_addr_t *ipaddr,
                                 void *callback_arg)
{
    LWIP_UNUSED_ARG(name);
    uint32_t seq = (uint32_t)(uintptr_t)callback_arg;
    if (seq != s_probe_seq) {
        return;
    }
    s_probe_in_flight = false;
    s_probe_result = (ipaddr != NULL) ? 0 : 1;
}

/* Probe the most critical dependency first: the configured MQTT broker when
 * it is a hostname, else a stable well-known name. */
static const char *network_probe_hostname(void)
{
    const char *host = g_sys_config.mqtt.broker_url;
    bool has_alpha = false;

    if (host[0] != '\0') {
        for (const char *p = host; *p != '\0'; p++) {
            if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')) {
                has_alpha = true;
                break;
            }
        }
    }
    return has_alpha ? host : PROBE_FALLBACK_HOST;
}

void network_get_probe_stats(network_probe_stats_t *out)
{
    if (out == NULL) {
        return;
    }
    out->probe_ok = s_probe_ok_count;
    out->probe_fail = s_probe_fail_count;
    out->suspected[0] = '\0';
    for (int i = 0; i < NET_IF_COUNT; i++) {
        if (s_probe_suspect[i]) {
            strncpy(out->suspected,
                    network_interface_name((net_if_type_t)i),
                    sizeof(out->suspected) - 1U);
            out->suspected[sizeof(out->suspected) - 1U] = '\0';
            break;
        }
    }
}

/**
 * @brief Find best available interface based on priority
 */
static net_if_type_t find_best_interface(void) {
    xSemaphoreTake(config_mutex, portMAX_DELAY);
    /* Find the enabled UP interface with the lowest configured priority. */
    net_if_type_t best = NET_IF_NONE;
    uint8_t best_priority = 255;
    for (int i = 0; i < NET_IF_COUNT; i++) {
            bool enabled = (i == NET_IF_ETH) ? g_network_config.eth.enabled :
                           (i == NET_IF_WIFI) ? g_network_config.wifi.enabled :
                           (i == NET_IF_LTE) ? g_network_config.lte.enabled : false;
            uint8_t prio = (i == NET_IF_ETH) ? g_network_config.eth.priority :
                           (i == NET_IF_WIFI) ? g_network_config.wifi.priority :
                           (i == NET_IF_LTE) ? g_network_config.lte.priority : 255;
            if (enabled && prio < best_priority &&
                g_interface_status[i] == NET_IF_STATUS_UP &&
                !s_probe_suspect[i]) {
                best = (net_if_type_t)i;
                best_priority = prio;
            }
    }
    xSemaphoreGive(config_mutex);
    return best;
}

/**
 * @brief Perform failover to best available interface
 */
static int apply_default_route(net_if_type_t best) {
    net_if_type_t previous = g_active_interface;
    err_t err;

    if (best == NET_IF_NONE) {
        if (previous == NET_IF_NONE) {
            return 0;
        }
        err = netifapi_netif_set_default(NULL);
        if (err != ERR_OK) {
            LOGE(TAG, "Default route clear failed: %s(%d), err=%d",
                 network_interface_name(previous), previous, (int)err);
            return -1;
        }
        g_active_interface = NET_IF_NONE;
        g_route_generation++;
        LOGW(TAG, "Default route lost: %s(%d) -> NONE(255) generation=%lu",
             network_interface_name(previous), previous,
             (unsigned long)g_route_generation);
        LOGI(TAG, "Default route applied: NONE(255) generation=%lu",
             (unsigned long)g_route_generation);
        if (g_route_callback != NULL) {
            g_route_callback(previous, NET_IF_NONE, g_route_generation,
                             g_route_callback_context);
        }
        return 0;
    }
    if (best >= NET_IF_COUNT || g_netif_array[best] == NULL) {
        return -1;
    }
    if (best == previous) {
        return 0;
    }

    err = netifapi_netif_set_default(g_netif_array[best]);
    if (err != ERR_OK) {
        LOGE(TAG, "Default route failed: %s(%d) -> %s(%d), err=%d",
             network_interface_name(previous), previous,
             network_interface_name(best), best, (int)err);
        return -1;
    }

    g_active_interface = best;
    g_route_generation++;
    LOGI(TAG, "Route event: %s(%d) -> %s(%d) generation=%lu",
         network_interface_name(previous), previous,
         network_interface_name(best), best,
         (unsigned long)g_route_generation);
    LOGI(TAG, "Default route applied: %s(%d) generation=%lu",
         network_interface_name(best), best,
         (unsigned long)g_route_generation);
    if (g_route_callback != NULL) {
        g_route_callback(previous, best, g_route_generation,
                         g_route_callback_context);
    }
    (void)tcpip_callback_with_block(apply_route_dns_cb,
                                    (void *)(uintptr_t)best, 1);
    return 0;
}

static void perform_failover(net_if_type_t best) {

    if (best == NET_IF_NONE) {
        LOGW(TAG, "Failover: All interfaces DOWN");
        (void)apply_default_route(NET_IF_NONE);
        return;
    }

    if (best != g_active_interface) {
        (void)apply_default_route(best);
        /* Do not judge a brand-new path immediately (see PROBE_GRACE_MS). */
        s_probe_next_at = xTaskGetTickCount() + pdMS_TO_TICKS(PROBE_GRACE_MS);
    } else {
        LOGD(TAG, "Failover: Already using best interface");
    }
}



static 
int network_config_apply_cb(void) {
    if (!s_network_changed) {
        LOGI(TAG, "Network configuration unchanged, skipping apply");
        return 0;
    }
    LOGI(TAG, "Network configuration applying...");
    extern void apply_netif_config_tcpip_cb(void *ctx);
    tcpip_callback(apply_netif_config_tcpip_cb, NULL);
    s_network_changed = false;
    return 0;
}
void network_config_notify_cb(void) {
    LOGI(TAG, "Network configuration changed. Reloading...");
    xEventGroupSetBits(network_events, NET_RELOAD_CONFIG_BIT);
}

void network_manager_init(void) {
    LOGI(TAG, "Network manager init");

    /* Create event group for network synchronization */
    network_events = xEventGroupCreate();
    config_mutex = xSemaphoreCreateMutex();
    if (network_events == NULL || config_mutex == NULL) {
        LOGE(TAG, "Failed to create network events or mutex!");
        return;
    }

    /* Initialize lwIP with callback */
    LOGI(TAG, "Starting lwIP initialization...");
    tcpip_init(lwip_init_done, NULL);

    /* Wait for lwIP to be ready before adding network interfaces */
    LOGI(TAG, "Waiting for lwIP...");
    xEventGroupWaitBits(network_events, LWIP_READY_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

    /* Initialize network drivers and register interfaces */

    /* Ethernet */
    if (g_network_config.eth.enabled) {
        LOGI(TAG, "Initializing ETH driver...");
        struct netif* eth_netif = eth_driver_init();
        if (eth_netif != NULL) {
            network_register_interface(NET_IF_ETH, eth_netif);
            g_interface_status[NET_IF_ETH] = NET_IF_STATUS_DEGRADED;

            /* DHCP is owned by network_task and starts after the PHY/link
             * state has been observed as UP. */
        }
    }

    /* WiFi is initialized by the ST67W6X startup task created by GatewayV2.
     * Registration and optional connection are completed from network_task
     * after that task has finished, so W6X_Init() cannot block this startup
     * path or run concurrently with a shell command. */

    /* LTE (placeholder - driver not implemented yet) */
    if (g_network_config.lte.enabled) {
        LOGI(TAG, "Initializing LTE driver...");
        struct netif* lte_netif = lte_driver_init();
        if (lte_netif != NULL) {
            network_register_interface(NET_IF_LTE, lte_netif);
            g_interface_status[NET_IF_LTE] = NET_IF_STATUS_DEGRADED;
        }
    }

    /* Set initial active interface to first available */
    (void)apply_default_route(find_best_interface());
}

void network_task(void *pvParameters) {
    (void)pvParameters;

    /* Wait for lwIP to be ready */
    network_wait_lwip_ready();

    uint32_t last_ip[NET_IF_COUNT] = { 0 };
    bool wifi_registered = false;
    bool wifi_connect_started = false;
    TickType_t wifi_connect_started_at = 0;
    TickType_t wifi_next_retry_at = 0;
    uint32_t wifi_retry_count = 0U;
    const TickType_t wifi_retry_initial = pdMS_TO_TICKS(2000U);
    const TickType_t wifi_retry_max = pdMS_TO_TICKS(30000U);
    const TickType_t wifi_connect_timeout = pdMS_TO_TICKS(15000U);
    TickType_t wifi_dhcp_started_at = 0;
    TickType_t wifi_dhcp_retry_at = 0;
    bool eth_dhcp_started = false;
    TickType_t eth_dhcp_started_at = 0;
    TickType_t eth_dhcp_retry_at = 0;
    uint32_t eth_dhcp_restarts = 0U;
    bool eth_link_sample = false;
    TickType_t eth_link_changed_at = 0;

    while (1) {
#if (ST67_ARCH == 2)
        if (!wifi_registered) {
            struct netif *wifi_netif = wifi_driver_get_netif();
            if (wifi_netif != NULL) {
                network_register_interface(NET_IF_WIFI, wifi_netif);
                g_interface_status[NET_IF_WIFI] = NET_IF_STATUS_DEGRADED;
                wifi_registered = true;
                LOGI(TAG, "WiFi interface registered after ST67W6X startup");
            }
        }

        if (wifi_registered) {
            network_interface_config_t wifi_cfg;
            wifi_state_t wifi_state = wifi_driver_get_state();
            TickType_t now = xTaskGetTickCount();
            if (network_interface_get_config(NET_IF_WIFI, &wifi_cfg) == 0 &&
                wifi_cfg.enabled && strlen(wifi_cfg.ssid) > 0) {
                if (g_wifi_manual_connect_requested) {
                    g_wifi_manual_connect_requested = false;
                    wifi_next_retry_at = now;
                    wifi_retry_count = 0U;
                    if (wifi_state == WIFI_STATE_CONNECTED ||
                        wifi_state == WIFI_STATE_GOT_IP) {
                        (void)wifi_driver_disconnect();
                    }
                    LOGI(TAG, "Manual WiFi connect request accepted");
                }
                if (wifi_connect_started && wifi_state == WIFI_STATE_CONNECTING &&
                    (now - wifi_connect_started_at) >= wifi_connect_timeout) {
                    LOGW(TAG, "WiFi connect timeout; recovering ST67");
                    (void)wifi_driver_recover();
                    wifi_connect_started = false;
                    wifi_retry_count++;
                    wifi_next_retry_at = now + ((wifi_retry_initial <<
                        (wifi_retry_count > 4U ? 4U : wifi_retry_count - 1U)) >
                        wifi_retry_max ? wifi_retry_max : (wifi_retry_initial <<
                        (wifi_retry_count > 4U ? 4U : wifi_retry_count - 1U)));
                }

                wifi_state = wifi_driver_get_state();
                if (!wifi_connect_started && wifi_state == WIFI_STATE_CONNECTING) {
                    /* A shell-issued connect request shares the same timeout
                     * protection as an automatically-started request. */
                    wifi_connect_started = true;
                    wifi_connect_started_at = now;
                    LOGI(TAG, "Tracking externally started WiFi connect");
                }
                if (wifi_state == WIFI_STATE_CONNECTED ||
                    wifi_state == WIFI_STATE_GOT_IP) {
                    wifi_connect_started = false;
                    wifi_retry_count = 0U;
                    wifi_next_retry_at = now + wifi_retry_initial;
                } else if (!wifi_connect_started &&
                           (wifi_state == WIFI_STATE_INIT ||
                            wifi_state == WIFI_STATE_DISCONNECTED ||
                            wifi_state == WIFI_STATE_ERROR) &&
                           (int32_t)(now - wifi_next_retry_at) >= 0) {
                    uint32_t delay_ms = 2000U <<
                        (wifi_retry_count > 4U ? 4U : wifi_retry_count);
                    if (delay_ms > 30000U) delay_ms = 30000U;
                    LOGI(TAG, "WiFi connect attempt %lu", wifi_retry_count + 1U);
#if (ST67_ARCH == 2)
                    /* A disconnect callback only posts the link-down work to
                     * tcpip. Complete the RX queue boundary before starting
                     * another ST67 transaction; otherwise stale DHCP frames
                     * can be consumed by the new XID. This runs in the
                     * network task, never in the tcpip callback. */
                    if (wifi_state == WIFI_STATE_DISCONNECTED ||
                        wifi_state == WIFI_STATE_ERROR) {
                        wifi_netif_request_rx_flush();
                        if (wifi_netif_wait_rx_flush(100U) != 0) {
                            LOGW(TAG, "WiFi RX flush timed out before reconnect");
                        }
                    }
#endif
                    if (wifi_driver_start() == 0 &&
                        wifi_driver_connect(wifi_cfg.ssid, wifi_cfg.password) == 0) {
                        wifi_connect_started = true;
                        wifi_connect_started_at = now;
                        LOGI(TAG, "WiFi connect request started");
                    } else {
                        wifi_connect_started = false;
                        wifi_retry_count++;
                        wifi_next_retry_at = now + pdMS_TO_TICKS(delay_ms);
                        LOGW(TAG, "WiFi retry scheduled in %lu ms", delay_ms);
                    }
                }
            }
        }

        if (g_netif_array[NET_IF_LTE] != NULL &&
            g_network_config.lte.enabled && !lte_driver_status()) {
            network_interface_config_t cfg;
            if (network_interface_get_config(NET_IF_LTE, &cfg) == 0)
                lte_driver_set_credentials(cfg.apn, cfg.username, cfg.password);
            (void)lte_driver_start();
        }

        /* Update status for all interfaces */
        for (int i = 0; i < NET_IF_COUNT; i++) {
            if (g_netif_array[i] != NULL) {
                if (i == NET_IF_ETH) {
                    bool raw_link = netif_is_link_up(g_netif_array[i]);
                    TickType_t now = xTaskGetTickCount();
                    if (raw_link != eth_link_sample) {
                        eth_link_sample = raw_link;
                        eth_link_changed_at = now;
                    } else if (raw_link != g_eth_link_confirmed &&
                               (now - eth_link_changed_at) >= pdMS_TO_TICKS(1000U)) {
                        g_eth_link_confirmed = raw_link;
                        LOGI(TAG, "ETH link stable: %s",
                             raw_link ? "UP" : "DOWN");
                    }
                }
                update_interface_status((net_if_type_t)i);

#if (ST67_ARCH == 2)
                if (i == NET_IF_WIFI && netif_is_link_up(g_netif_array[i])) {
                    TickType_t now = xTaskGetTickCount();
                    if (netif_ip4_addr(g_netif_array[i])->addr != 0) {
                        wifi_dhcp_started_at = 0;
                        wifi_dhcp_retry_at = 0;
                    } else if (wifi_dhcp_started_at == 0) {
                        wifi_dhcp_started_at = now;
                    } else if ((now - wifi_dhcp_started_at) >= pdMS_TO_TICKS(15000U) &&
                               (int32_t)(now - wifi_dhcp_retry_at) >= 0) {
                        wifi_netif_diag_t diag;
                        wifi_netif_get_diag(&diag);
                        LOGW(TAG, "WiFi DHCP timeout: state=%u tx=%lu rx=%lu offer=%lu ack=%lu nack=%lu xid=0x%08lx "
                             "notify=%lu packets=%lu empty=%lu input_err=%lu "
                             "bound=%u rebind=%lu rebind_fail=%lu "
                             "spi_rx=0x%08lx%08lx spi_drop=0x%08lx%08lx "
                             "spi_io=0x%08lx%08lx spi_hdr=0x%08lx%08lx "
                             "spi_stall=0x%08lx%08lx",
                             diag.dhcp_state, (unsigned long)diag.dhcp_tx,
                             (unsigned long)diag.dhcp_rx,
                             (unsigned long)diag.dhcp_offer,
                             (unsigned long)diag.dhcp_ack,
                             (unsigned long)diag.dhcp_nack,
                             (unsigned long)diag.dhcp_lwip_xid,
                             (unsigned long)diag.rx_notify,
                             (unsigned long)diag.rx_packets,
                             (unsigned long)diag.rx_empty,
                             (unsigned long)diag.rx_input_errors,
                             (unsigned)diag.transport_bound,
                             (unsigned long)diag.transport_rebinds,
                             (unsigned long)diag.transport_rebind_failures,
                             (unsigned long)(diag.spi_rx_packets >> 32),
                             (unsigned long)diag.spi_rx_packets,
                             (unsigned long)(diag.spi_rx_drops >> 32),
                             (unsigned long)diag.spi_rx_drops,
                             (unsigned long)(diag.spi_io_errors >> 32),
                             (unsigned long)diag.spi_io_errors,
                             (unsigned long)(diag.spi_header_errors >> 32),
                             (unsigned long)diag.spi_header_errors,
                             (unsigned long)(diag.spi_rx_stalls >> 32),
                             (unsigned long)diag.spi_rx_stalls);
                        (void)wifi_netif_restart_dhcp();
                        wifi_dhcp_started_at = now;
                        wifi_dhcp_retry_at = now + pdMS_TO_TICKS(30000U);
                    }
                } else if (i == NET_IF_WIFI) {
                    wifi_dhcp_started_at = 0;
                    wifi_dhcp_retry_at = 0;
                }
#endif

                /* Log IP changes */
                uint32_t current_ip = netif_ip4_addr(g_netif_array[i])->addr;
                if (current_ip != 0 && current_ip != last_ip[i]) {
                    char ip_str[16];
                    ip4addr_ntoa_r(netif_ip4_addr(g_netif_array[i]), ip_str, sizeof(ip_str));
                    LOGI(TAG, "Interface %d UP, IP: %s", i, ip_str);
                    last_ip[i] = current_ip;
                }
            }
        }

        struct netif *eth_nif = g_netif_array[NET_IF_ETH];
        if (eth_nif != NULL) {
            network_interface_config_t eth_cfg;
            (void)network_interface_get_config(NET_IF_ETH, &eth_cfg);
            if (eth_cfg.dhcp && g_eth_link_confirmed && !eth_dhcp_started) {
                err_t err = netifapi_dhcp_start(eth_nif);
                if (err == ERR_OK) {
                    eth_dhcp_started = true;
                    eth_dhcp_started_at = xTaskGetTickCount();
                    eth_dhcp_retry_at = 0;
                    LOGI(TAG, "ETH: DHCP started after link UP");
                } else {
                    LOGE(TAG, "ETH: DHCP start failed: %d", (int)err);
                }
            } else if ((!eth_cfg.dhcp || !g_eth_link_confirmed) && eth_dhcp_started) {
                (void)netifapi_dhcp_stop(eth_nif);
                eth_dhcp_started = false;
                eth_dhcp_started_at = 0;
                eth_dhcp_retry_at = 0;
                LOGI(TAG, "ETH: DHCP stopped after link DOWN/config change");
            }

            /* Watchdog: mirror the WiFi DHCP timeout handling so a silent
             * DHCP server surfaces as periodic WARNs instead of an
             * interface silently stuck in DEGRADED forever. */
            if (eth_cfg.dhcp && g_eth_link_confirmed && eth_dhcp_started) {
                TickType_t now = xTaskGetTickCount();
                if (netif_ip4_addr(eth_nif)->addr != 0U) {
                    eth_dhcp_started_at = 0;
                    eth_dhcp_retry_at = 0;
                } else if (eth_dhcp_started_at == 0) {
                    eth_dhcp_started_at = now;
                } else if ((now - eth_dhcp_started_at) >= pdMS_TO_TICKS(15000U) &&
                           (int32_t)(now - eth_dhcp_retry_at) >= 0) {
                    ethernetif_diag_t diag;
                    ethernetif_get_diag(&diag);
                    LOGW(TAG, "ETH DHCP timeout: state=%u tx=%lu rx=%lu "
                         "discover=%lu offer=%lu ack=%lu nack=%lu "
                         "last_xid=0x%08lx lwip_xid=0x%08lx restarts=%lu",
                         (unsigned)diag.dhcp_state,
                         (unsigned long)diag.dhcp_tx,
                         (unsigned long)diag.dhcp_rx,
                         (unsigned long)diag.dhcp_discover,
                         (unsigned long)diag.dhcp_offer,
                         (unsigned long)diag.dhcp_ack,
                         (unsigned long)diag.dhcp_nack,
                         (unsigned long)diag.dhcp_last_xid,
                         (unsigned long)diag.dhcp_lwip_xid,
                         (unsigned long)eth_dhcp_restarts);
                    (void)netifapi_dhcp_stop(eth_nif);
                    (void)netifapi_dhcp_start(eth_nif);
                    eth_dhcp_restarts++;
                    eth_dhcp_started_at = now;
                    eth_dhcp_retry_at = now + pdMS_TO_TICKS(30000U);
                }
            }
        }

        /* Periodic network status update. */
#endif
        TickType_t net_delay_ticks = pdMS_TO_TICKS(1000);
        vTaskDelay(net_delay_ticks);
    }
}

void network_failover_task(void *pvParameters) {
    (void)pvParameters;

    /* Wait for lwIP and initial network setup */
    network_wait_lwip_ready();
    vTaskDelay(pdMS_TO_TICKS(2000));  /* Wait for initial DHCP */

    uint32_t failover_timer = 0;
    const uint32_t DEBOUNCE_TICKS = 6;  /* 6 * 500ms = 3 seconds debounce */

    while (1) {
        /* Check for interface status changes (clear bits, wait up to 500ms) */
        EventBits_t uxBits = xEventGroupWaitBits(
            network_events,
            ETH_STATUS_BIT | WIFI_STATUS_BIT | LTE_STATUS_BIT | NET_RELOAD_CONFIG_BIT,
            pdTRUE,  /* Clear bits after reading */
            pdFALSE,
            pdMS_TO_TICKS(500)
        );

        if (uxBits & NET_RELOAD_CONFIG_BIT) {
            LOGI(TAG, "Network configuration reloaded. Applying dynamically...");
            extern void apply_netif_config_tcpip_cb(void *ctx);
            tcpip_callback(apply_netif_config_tcpip_cb, NULL);
        }

        /* --- Active-route reachability probing --- */
        TickType_t probe_now = xTaskGetTickCount();
        net_if_type_t probe_active = g_active_interface;

        /* Suspicion expires so a healed path gets re-elected later.  The
         * wait grows with each consecutive demotion of that interface. */
        for (int i = 0; i < NET_IF_COUNT; i++) {
            if (s_probe_suspect[i] &&
                (probe_now - s_probe_suspect_at[i]) >= s_probe_suspect_ttl[i]) {
                LOGI(TAG, "%s(%d) reachability suspicion expired",
                     network_interface_name((net_if_type_t)i), i);
                s_probe_suspect[i] = false;
            }
        }

        /* Fresh status events (link/DHCP change) clear the suspicion flag so
         * a healed path can be re-elected.  The demote counter is
         * deliberately NOT reset here: on guest/mesh APs every forced
         * reassociation itself produces status churn, which would wipe the
         * backoff history each cycle and re-enable the flap loop.  The
         * counter only resets on a successful probe of that path. */
        if ((uxBits & ETH_STATUS_BIT) != 0U) {
            s_probe_suspect[NET_IF_ETH] = false;
        }
        if ((uxBits & WIFI_STATUS_BIT) != 0U) {
            s_probe_suspect[NET_IF_WIFI] = false;
        }
        if ((uxBits & LTE_STATUS_BIT) != 0U) {
            s_probe_suspect[NET_IF_LTE] = false;
        }

        /* Launch one async DNS probe through the current default route. */
        if (!s_probe_in_flight && probe_active != NET_IF_NONE &&
            (int32_t)(probe_now - s_probe_next_at) >= 0) {
            const char *host = network_probe_hostname();
            s_probe_next_at = probe_now + pdMS_TO_TICKS(PROBE_PERIOD_MS);
            s_probe_seq++;
            s_probe_result = -1;
            s_probe_in_flight = true;
            s_probe_sent_at = probe_now;
            err_t probe_err = dns_gethostbyname(host, &s_probe_addr,
                                                network_probe_dns_cb,
                                                (void *)(uintptr_t)s_probe_seq);
            if (probe_err == ERR_ARG || probe_err == ERR_MEM) {
                /* Immediate reject counts as a failed probe. */
                s_probe_in_flight = false;
                s_probe_result = 1;
            }
            /* ERR_OK: cached answer, callback already ran synchronously.
             * ERR_INPROGRESS: reply arrives from the tcpip thread. */
        }

        /* Timeout guard: never let an unanswered probe block the pipeline. */
        if (s_probe_in_flight &&
            (probe_now - s_probe_sent_at) >= pdMS_TO_TICKS(PROBE_TIMEOUT_MS)) {
            LOGW(TAG, "Reachability probe timed out after %u ms",
                 (unsigned)PROBE_TIMEOUT_MS);
            s_probe_in_flight = false;
            s_probe_result = 1;
        }

        /* Consume the finished result. */
        if (!s_probe_in_flight && s_probe_result >= 0) {
            int32_t res = s_probe_result;
            s_probe_result = -1;
            if (res == 0) {
                s_probe_ok_count++;
                s_probe_fail_streak = 0U;
                if (probe_active != NET_IF_NONE) {
                    s_probe_demotes[probe_active] = 0U;
                }
            } else {
                s_probe_fail_count++;
                s_probe_fail_streak++;
                if (probe_active != NET_IF_NONE &&
                    s_probe_fail_streak >= PROBE_FAIL_THRESHOLD &&
                    !s_probe_suspect[probe_active]) {
                    uint32_t demotes = ++s_probe_demotes[probe_active];
                    uint32_t shift = demotes - 1U;
                    if (shift > 3U) {
                        shift = 3U;
                    }
                    TickType_t ttl_ms =
                        (TickType_t)(PROBE_SUSPECT_TTL_MS << shift);
                    if (ttl_ms > (TickType_t)PROBE_SUSPECT_TTL_MAX_MS) {
                        ttl_ms = (TickType_t)PROBE_SUSPECT_TTL_MAX_MS;
                    }
                    s_probe_suspect[probe_active] = true;
                    s_probe_suspect_at[probe_active] = probe_now;
                    s_probe_suspect_ttl[probe_active] = ttl_ms;
                    LOGW(TAG,
                         "Active route %s(%d) failed %u reachability probes; "
                         "demoting (backoff=%lums, demotes=%lu)",
                         network_interface_name(probe_active), probe_active,
                         (unsigned)s_probe_fail_streak,
                         (unsigned long)ttl_ms, (unsigned long)demotes);
                    /* NOTE: no forced disassociation here.  Field testing on
                     * a guest AP showed the rescan itself triggers a DNS
                     * warm-up blackout, which re-failed the probes and
                     * created an endless demote/reassociate loop.  The
                     * exponential backoff above is the recovery mechanism;
                     * manual `wifi connect` remains available for roaming. */
                }
            }
        }

        net_if_type_t active = g_active_interface;
        net_if_type_t best = find_best_interface();
        bool active_lost = active != NET_IF_NONE &&
                           (active >= NET_IF_COUNT ||
                            g_interface_status[active] != NET_IF_STATUS_UP);

        /* Loss of the currently selected path is not debounced. Keeping a
         * dead default netif selected stalls TCP teardown and can fill the
         * tcpip mailbox while packets continue to arrive on WiFi/LTE.
         * Debounce only promotion/preference changes while the active path
         * remains healthy. */
        if (active_lost) {
            LOGW(TAG, "Active route %s(%d) is no longer UP; failover now",
                 network_interface_name(active), active);
            perform_failover(best);
            failover_timer = 0U;
        } else if (best != active) {
            failover_timer++;
            if (failover_timer >= DEBOUNCE_TICKS) {
                perform_failover(best);
                failover_timer = 0;
            }
        } else {
            failover_timer = 0;
        }
    }
}

/*******************************************************************************
 * Interface Management Implementation
 ******************************************************************************/

int network_interface_enable(net_if_type_t if_type) {
    if (if_type >= NET_IF_COUNT) return -1;

    xSemaphoreTake(config_mutex, portMAX_DELAY);
    switch (if_type) {
        case NET_IF_ETH:
            g_network_config.eth.enabled = true;
            break;
        case NET_IF_WIFI:
            g_network_config.wifi.enabled = true;
            break;
        case NET_IF_LTE:
            g_network_config.lte.enabled = true;
            break;
        default:
            xSemaphoreGive(config_mutex);
            return -1;
    }
    xSemaphoreGive(config_mutex);

    LOGI(TAG, "Interface %d enabled", if_type);
    return 0;
}

int network_interface_disable(net_if_type_t if_type) {
    if (if_type >= NET_IF_COUNT) return -1;

    xSemaphoreTake(config_mutex, portMAX_DELAY);
    switch (if_type) {
        case NET_IF_ETH:
            g_network_config.eth.enabled = false;
            break;
        case NET_IF_WIFI:
            g_network_config.wifi.enabled = false;
            break;
        case NET_IF_LTE:
            g_network_config.lte.enabled = false;
            break;
        default:
            xSemaphoreGive(config_mutex);
            return -1;
    }
    xSemaphoreGive(config_mutex);

    LOGI(TAG, "Interface %d disabled", if_type);
    return 0;
}

net_if_status_t network_interface_get_status(net_if_type_t if_type) {
    if (if_type >= NET_IF_COUNT) return NET_IF_STATUS_DOWN;
    return g_interface_status[if_type];
}

struct netif* network_interface_get_netif(net_if_type_t if_type) {
    if (if_type >= NET_IF_COUNT) return NULL;
    return g_netif_array[if_type];
}

int network_interface_set_config(net_if_type_t if_type, const network_interface_config_t *config) {
    if (if_type >= NET_IF_COUNT || config == NULL) return -1;

    xSemaphoreTake(config_mutex, portMAX_DELAY);
    switch (if_type) {
        case NET_IF_ETH:
            memcpy(&g_network_config.eth, config, sizeof(network_interface_config_t));
            break;
        case NET_IF_WIFI:
            memcpy(&g_network_config.wifi, config, sizeof(network_interface_config_t));
            break;
        case NET_IF_LTE:
            memcpy(&g_network_config.lte, config, sizeof(network_interface_config_t));
            break;
        default:
            xSemaphoreGive(config_mutex);
            return -1;
    }
    xSemaphoreGive(config_mutex);

    return 0;
}

void apply_netif_config_tcpip_cb(void *ctx) {
    (void)ctx;
    LOGI(TAG, "Applying network config in tcpip thread...");
    xSemaphoreTake(config_mutex, portMAX_DELAY);
    
    /* ETH */
    struct netif *eth_nif = g_netif_array[NET_IF_ETH];
    if (eth_nif) {
        if (!g_network_config.eth.enabled) {
            netif_set_down(eth_nif);
        } else {
            if (g_network_config.eth.dhcp) {
                netif_set_up(eth_nif);
                dhcp_start(eth_nif);
            } else {
                dhcp_stop(eth_nif);
                ip4_addr_t ip, mask, gw;
                ip.addr = g_network_config.eth.static_ip.ip;
                mask.addr = g_network_config.eth.static_ip.netmask;
                gw.addr = g_network_config.eth.static_ip.gateway;
                netif_set_addr(eth_nif, &ip, &mask, &gw);
                netif_set_up(eth_nif);
                
                ip4_addr_t dns1, dns2;
                dns1.addr = g_network_config.eth.static_ip.dns_primary;
                dns2.addr = g_network_config.eth.static_ip.dns_secondary;
                if (dns1.addr != 0) dns_setserver(0, (const ip_addr_t *)&dns1);
                if (dns2.addr != 0) dns_setserver(1, (const ip_addr_t *)&dns2);
            }
        }
    }
    
    /* WIFI */
    struct netif *wifi_nif = g_netif_array[NET_IF_WIFI];
    if (wifi_nif) {
        if (!g_network_config.wifi.enabled) {
            netif_set_down(wifi_nif);
        } else {
            if (g_network_config.wifi.dhcp) {
                netif_set_up(wifi_nif);
                dhcp_start(wifi_nif);
            } else {
                dhcp_stop(wifi_nif);
                ip4_addr_t ip, mask, gw;
                ip.addr = g_network_config.wifi.static_ip.ip;
                mask.addr = g_network_config.wifi.static_ip.netmask;
                gw.addr = g_network_config.wifi.static_ip.gateway;
                netif_set_addr(wifi_nif, &ip, &mask, &gw);
                netif_set_up(wifi_nif);
                
                ip4_addr_t dns1, dns2;
                dns1.addr = g_network_config.wifi.static_ip.dns_primary;
                dns2.addr = g_network_config.wifi.static_ip.dns_secondary;
                if (dns1.addr != 0) dns_setserver(0, (const ip_addr_t *)&dns1);
                if (dns2.addr != 0) dns_setserver(1, (const ip_addr_t *)&dns2);
            }
        }
    }
    
    xSemaphoreGive(config_mutex);
}

int network_interface_get_config(net_if_type_t if_type, network_interface_config_t *config) {
    if (if_type >= NET_IF_COUNT || config == NULL) return -1;

    xSemaphoreTake(config_mutex, portMAX_DELAY);
    switch (if_type) {
        case NET_IF_ETH:
            memcpy(config, &g_network_config.eth, sizeof(network_interface_config_t));
            break;
        case NET_IF_WIFI:
            memcpy(config, &g_network_config.wifi, sizeof(network_interface_config_t));
            break;
        case NET_IF_LTE:
            memcpy(config, &g_network_config.lte, sizeof(network_interface_config_t));
            break;
        default:
            xSemaphoreGive(config_mutex);
            return -1;
    }
    xSemaphoreGive(config_mutex);

    return 0;
}

static void add_ip_to_json(cJSON *obj, const char *key, uint32_t ip) {
    ip4_addr_t addr;
    addr.addr = ip;
    cJSON_AddStringToObject(obj, key, ip4addr_ntoa(&addr));
}

static void parse_ip_from_json(cJSON *obj, const char *key, uint32_t *ip) {
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (cJSON_IsString(item)) {
        ip4_addr_t addr;
        if (ipaddr_aton(item->valuestring, &addr)) {
            *ip = addr.addr;
        }
    }
}

void network_config_save_cb(cJSON *root, bool include_secrets) {
    if (!root) return;
    
    cJSON *network = cJSON_CreateObject();
    
    /* Save priority */
    cJSON *priority = cJSON_CreateArray();
    const char *ifs[NET_IF_COUNT];
    for(int i = 0; i < NET_IF_COUNT; i++) ifs[i] = "";
    
    if (g_network_config.eth.priority < NET_IF_COUNT) ifs[g_network_config.eth.priority] = "eth";
    if (g_network_config.wifi.priority < NET_IF_COUNT) ifs[g_network_config.wifi.priority] = "wifi";
    if (g_network_config.lte.priority < NET_IF_COUNT) ifs[g_network_config.lte.priority] = "lte";
    
    for (int i = 0; i < NET_IF_COUNT; i++) {
        if (strlen(ifs[i]) > 0) {
            cJSON_AddItemToArray(priority, cJSON_CreateString(ifs[i]));
        }
    }
    cJSON_AddItemToObject(network, "priority", priority);
    
    /* ETH Config */
    cJSON *eth = cJSON_CreateObject();
    cJSON_AddBoolToObject(eth, "enabled", g_network_config.eth.enabled);
    cJSON_AddBoolToObject(eth, "dhcp", g_network_config.eth.dhcp);
    add_ip_to_json(eth, "ip", g_network_config.eth.static_ip.ip);
    add_ip_to_json(eth, "netmask", g_network_config.eth.static_ip.netmask);
    add_ip_to_json(eth, "gateway", g_network_config.eth.static_ip.gateway);
    add_ip_to_json(eth, "dns_primary", g_network_config.eth.static_ip.dns_primary);
    add_ip_to_json(eth, "dns_secondary", g_network_config.eth.static_ip.dns_secondary);
    cJSON_AddItemToObject(network, "eth", eth);
    
    /* WiFi Config */
    cJSON *wifi = cJSON_CreateObject();
    cJSON_AddBoolToObject(wifi, "enabled", g_network_config.wifi.enabled);
    cJSON_AddBoolToObject(wifi, "dhcp", g_network_config.wifi.dhcp);
    cJSON_AddStringToObject(wifi, "ssid", g_network_config.wifi.ssid);
    cJSON_AddBoolToObject(wifi, "password_set", g_network_config.wifi.password[0] != '\0');
    if (include_secrets) {
        cJSON_AddStringToObject(wifi, "password", g_network_config.wifi.password);
    }
    add_ip_to_json(wifi, "ip", g_network_config.wifi.static_ip.ip);
    add_ip_to_json(wifi, "netmask", g_network_config.wifi.static_ip.netmask);
    add_ip_to_json(wifi, "gateway", g_network_config.wifi.static_ip.gateway);
    add_ip_to_json(wifi, "dns_primary", g_network_config.wifi.static_ip.dns_primary);
    add_ip_to_json(wifi, "dns_secondary", g_network_config.wifi.static_ip.dns_secondary);
    cJSON_AddItemToObject(network, "wifi", wifi);
    
    /* LTE Config */
    cJSON *lte = cJSON_CreateObject();
    cJSON_AddBoolToObject(lte, "enabled", g_network_config.lte.enabled);
    cJSON_AddStringToObject(lte, "apn", g_network_config.lte.apn);
    cJSON_AddStringToObject(lte, "username", g_network_config.lte.username);
    cJSON_AddBoolToObject(lte, "password_set", g_network_config.lte.password[0] != '\0');
    if (include_secrets) {
        cJSON_AddStringToObject(lte, "password", g_network_config.lte.password);
    }
    cJSON_AddItemToObject(network, "lte", lte);

    cJSON_AddItemToObject(root, "network", network);
}

void network_config_load_cb(cJSON *node) {
    if (config_mutex) xSemaphoreTake(config_mutex, portMAX_DELAY);
    memcpy(&s_staging_network_config, &g_network_config, sizeof(network_config_t));
    if (config_mutex) xSemaphoreGive(config_mutex);
    if (!node) {
        LOGW(TAG, "No network config node found, using defaults");
        return;
    }
    
    /* Parse Priority */
    cJSON *priority = cJSON_GetObjectItem(node, "priority");
    if (cJSON_IsArray(priority)) {
        for (int i = 0; i < NET_IF_COUNT && i < cJSON_GetArraySize(priority); i++) {
            cJSON *p_item = cJSON_GetArrayItem(priority, i);
            if (cJSON_IsString(p_item)) {
                if (strcmp(p_item->valuestring, "eth") == 0) s_staging_network_config.eth.priority = i;
                else if (strcmp(p_item->valuestring, "wifi") == 0) s_staging_network_config.wifi.priority = i;
                else if (strcmp(p_item->valuestring, "lte") == 0) s_staging_network_config.lte.priority = i;
            }
        }
    } else if (cJSON_IsString(priority) && priority->valuestring[0] != '\0') {
        /* The web dashboard posts the priority as a CSV string
         * ("eth,wifi,lte").  Accept it next to the canonical array form;
         * every token consumes one slot so the semantics stay identical. */
        const char *cursor = priority->valuestring;
        int slot = 0;
        while (*cursor != '\0' && slot < NET_IF_COUNT) {
            while (*cursor == ' ' || *cursor == ',') cursor++;
            if (*cursor == '\0') break;
            const char *start = cursor;
            while (*cursor != '\0' && *cursor != ',') cursor++;
            size_t len = (size_t)(cursor - start);
            if (len == 3 && strncmp(start, "eth", 3) == 0) s_staging_network_config.eth.priority = (uint8_t)slot;
            else if (len == 4 && strncmp(start, "wifi", 4) == 0) s_staging_network_config.wifi.priority = (uint8_t)slot;
            else if (len == 3 && strncmp(start, "lte", 3) == 0) s_staging_network_config.lte.priority = (uint8_t)slot;
            slot++;
        }
    }
    
    /* Parse ETH */
    cJSON *eth = cJSON_GetObjectItem(node, "eth");
    if (eth) {
        cJSON *enabled = cJSON_GetObjectItem(eth, "enabled");
        if (cJSON_IsBool(enabled) ) s_staging_network_config.eth.enabled = cJSON_IsTrue(enabled);
        cJSON *dhcp = cJSON_GetObjectItem(eth, "dhcp");
        if (cJSON_IsBool(dhcp) ) s_staging_network_config.eth.dhcp = cJSON_IsTrue(dhcp);
        parse_ip_from_json(eth, "ip", &s_staging_network_config.eth.static_ip.ip);
        parse_ip_from_json(eth, "netmask", &s_staging_network_config.eth.static_ip.netmask);
        parse_ip_from_json(eth, "gateway", &s_staging_network_config.eth.static_ip.gateway);
        parse_ip_from_json(eth, "dns_primary", &s_staging_network_config.eth.static_ip.dns_primary);
        parse_ip_from_json(eth, "dns_secondary", &s_staging_network_config.eth.static_ip.dns_secondary);
    }
    
    /* Parse WiFi */
    cJSON *wifi = cJSON_GetObjectItem(node, "wifi");
    if (wifi) {
        cJSON *enabled = cJSON_GetObjectItem(wifi, "enabled");
        if (cJSON_IsBool(enabled) ) s_staging_network_config.wifi.enabled = cJSON_IsTrue(enabled);
        cJSON *dhcp = cJSON_GetObjectItem(wifi, "dhcp");
        if (cJSON_IsBool(dhcp) ) s_staging_network_config.wifi.dhcp = cJSON_IsTrue(dhcp);
        cJSON *ssid = cJSON_GetObjectItem(wifi, "ssid");
        if (cJSON_IsString(ssid)) {
            strncpy(s_staging_network_config.wifi.ssid, ssid->valuestring, sizeof(s_staging_network_config.wifi.ssid) - 1);
            s_staging_network_config.wifi.ssid[sizeof(s_staging_network_config.wifi.ssid) - 1] = '\0';
        }
        cJSON *pwd = cJSON_GetObjectItem(wifi, "password");
        if (cJSON_IsString(pwd) && pwd->valuestring[0] != '\0') {
            strncpy(s_staging_network_config.wifi.password, pwd->valuestring, sizeof(s_staging_network_config.wifi.password) - 1);
            s_staging_network_config.wifi.password[sizeof(s_staging_network_config.wifi.password) - 1] = '\0';
        }
        parse_ip_from_json(wifi, "ip", &s_staging_network_config.wifi.static_ip.ip);
        parse_ip_from_json(wifi, "netmask", &s_staging_network_config.wifi.static_ip.netmask);
        parse_ip_from_json(wifi, "gateway", &s_staging_network_config.wifi.static_ip.gateway);
        parse_ip_from_json(wifi, "dns_primary", &s_staging_network_config.wifi.static_ip.dns_primary);
        parse_ip_from_json(wifi, "dns_secondary", &s_staging_network_config.wifi.static_ip.dns_secondary);
    }
    
    /* Parse LTE */
    cJSON *lte = cJSON_GetObjectItem(node, "lte");
    if (lte) {
        cJSON *enabled = cJSON_GetObjectItem(lte, "enabled");
        if (cJSON_IsBool(enabled) ) s_staging_network_config.lte.enabled = cJSON_IsTrue(enabled);
        cJSON *apn = cJSON_GetObjectItem(lte, "apn");
        if (cJSON_IsString(apn)) {
            strncpy(s_staging_network_config.lte.apn, apn->valuestring, sizeof(s_staging_network_config.lte.apn) - 1);
            s_staging_network_config.lte.apn[sizeof(s_staging_network_config.lte.apn) - 1] = '\0';
        }
        cJSON *username = cJSON_GetObjectItem(lte, "username");
        if (cJSON_IsString(username)) {
            strncpy(s_staging_network_config.lte.username, username->valuestring, sizeof(s_staging_network_config.lte.username) - 1);
            s_staging_network_config.lte.username[sizeof(s_staging_network_config.lte.username) - 1] = '\0';
        }
        cJSON *password = cJSON_GetObjectItem(lte, "password");
        if (cJSON_IsString(password) && password->valuestring[0] != '\0') {
            strncpy(s_staging_network_config.lte.password, password->valuestring, sizeof(s_staging_network_config.lte.password) - 1);
            s_staging_network_config.lte.password[sizeof(s_staging_network_config.lte.password) - 1] = '\0';
        }
    }
}


void network_config_commit_cb(void) {
    if (config_mutex) xSemaphoreTake(config_mutex, portMAX_DELAY);
    if (memcmp(&g_network_config, &s_staging_network_config, sizeof(network_config_t)) != 0) {
        memcpy(&g_network_config, &s_staging_network_config, sizeof(network_config_t));
        s_network_changed = true;
    } else {
        s_network_changed = false;
    }
    if (config_mutex) xSemaphoreGive(config_mutex);
}

void network_config_register(void) {
    config_manager_register_node("network", network_config_load_cb, network_config_save_cb, network_config_notify_cb, network_config_commit_cb, network_config_apply_cb);
}

int network_manager_get_config(network_config_t *config) {
    if (config == NULL) return -1;
    xSemaphoreTake(config_mutex, portMAX_DELAY);
    memcpy(config, &g_network_config, sizeof(network_config_t));
    xSemaphoreGive(config_mutex);
    return 0;
}

/*******************************************************************************
 * Failover Implementation
 ******************************************************************************/

int network_set_primary_interface(net_if_type_t if_type) {
    if (if_type >= NET_IF_COUNT) return -1;

    xSemaphoreTake(config_mutex, portMAX_DELAY);
    /* Update priority - set selected interface to highest priority */
    switch (if_type) {
        case NET_IF_ETH:
            g_network_config.eth.priority = 0;
            g_network_config.wifi.priority = 1;
            g_network_config.lte.priority = 2;
            break;
        case NET_IF_WIFI:
            g_network_config.eth.priority = 1;
            g_network_config.wifi.priority = 0;
            g_network_config.lte.priority = 2;
            break;
        case NET_IF_LTE:
            g_network_config.eth.priority = 1;
            g_network_config.wifi.priority = 2;
            g_network_config.lte.priority = 0;
            break;
        default:
            xSemaphoreGive(config_mutex);
            return -1;
    }
    xSemaphoreGive(config_mutex);

    LOGI(TAG, "Primary interface set to %d", if_type);
    return 0;
}

net_if_type_t network_get_active_interface(void) {
    return g_active_interface;
}

uint32_t network_get_route_generation(void) {
    return g_route_generation;
}

int network_manager_register_route_callback(network_route_callback_t callback,
                                            void *context) {
    g_route_callback = callback;
    g_route_callback_context = context;
    return 0;
}

int network_switch_to_interface(net_if_type_t if_type) {
    if (if_type >= NET_IF_COUNT) return -1;
    if (g_interface_status[if_type] != NET_IF_STATUS_UP) {
        LOGW(TAG, "Cannot switch to interface %d - not available", if_type);
        return -1;
    }

    return apply_default_route(if_type);
}

/*******************************************************************************
 * Status Implementation
 ******************************************************************************/

net_state_t network_get_state(net_if_type_t if_type) {
    if (if_type >= NET_IF_COUNT) return NET_STATE_DOWN;

    if (g_interface_status[if_type] == NET_IF_STATUS_UP) {
        return NET_STATE_UP;
    } else if (g_interface_status[if_type] == NET_IF_STATUS_DEGRADED) {
        return NET_STATE_CONNECTING;
    }
    return NET_STATE_DOWN;
}

const char* network_get_ip_address(net_if_type_t if_type) {
    static char ip_str[16];

    if (if_type == NET_IF_WIFI) {
#if (ST67_ARCH == 1)
        uint8_t ip[4] = {0}, gw[4] = {0}, mask[4] = {0};
        if (wifi_driver_get_ip(ip, gw, mask) == 0 && (ip[0] != 0 || ip[1] != 0 || ip[2] != 0 || ip[3] != 0)) {
            snprintf(ip_str, sizeof(ip_str), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
            return ip_str;
        }
#endif
    }

    if (if_type >= NET_IF_COUNT || g_netif_array[if_type] == NULL) {
        return "0.0.0.0";
    }

    uint32_t ip = netif_ip4_addr(g_netif_array[if_type])->addr;
    if (ip != 0) {
        ip4addr_ntoa_r((const ip4_addr_t*)&ip, ip_str, sizeof(ip_str));
        return ip_str;
    }

    return "0.0.0.0";
}

/* Legacy API */
int network_set_priority(net_if_type_t primary, net_if_type_t secondary) {
    (void)secondary;
    return network_set_primary_interface(primary);
}
