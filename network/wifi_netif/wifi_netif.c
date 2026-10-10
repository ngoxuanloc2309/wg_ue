/**
 * @file wifi_netif.c
 * @brief lwIP netif adapter for ST67W611 (T02 architecture)
 *
 * Architecture: T02 - lwIP runs on STM32 host, ST67W611 is NCP
 *
 * TX/RX path: lwIP ↔ st67w611_lwip adapter ↔ ST67W611.
 *
 * The RX path runs in a FreeRTOS task that waits for notifications from
 * the ST67W611 library via netif_sta_notify_callback.
 */
#include "wifi_netif.h"
#include "wifi_driver.h"
#include "logger.h"
#include "st67w611_lwip.h"
#include "st67w611_lwip_internal.h"
#include "lwip/netif.h"
#include "lwip/etharp.h"
#include "lwip/dhcp.h"
#include "lwip/prot/dhcp.h"
#include "lwip/netifapi.h"
#include "lwip/dns.h"
#include "lwip/def.h"
#include "lwip/tcpip.h"
#include "lwip/timeouts.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <string.h>
#include "network_manager.h"

static const char *TAG = "wifi_netif";

/* RX task configuration */
#define WIFI_NETIF_TASK_STACK  3072U
#define WIFI_NETIF_TASK_PRIO  22U
#define WIFI_NETIF_RX_POLL_MS  100U
#define WIFI_NETIF_RX_BUDGET   8U

/* Netif RX ready event bits */
#define NETIF_STA_RX_RDY  (1UL << 0U)

/* pbuf_custom + driver buffer container */
typedef struct {
    struct pbuf_custom pb;
    void *buffer;
} netif_pbuf_t;

/* Module-level state */
static TaskHandle_t g_rx_task_handle = NULL;
static struct netif *g_wifi_netif = NULL;
static volatile wifi_netif_diag_t g_wifi_diag;
static uint32_t g_wifi_dns_primary;
static uint32_t g_wifi_dns_secondary;
static SemaphoreHandle_t g_rx_flush_done;
static volatile bool g_rx_flush_requested;
static volatile bool g_rx_flush_pending;

static uint32_t wifi_netif_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static void wifi_netif_record_dhcp(const uint8_t *payload, uint16_t len,
                                   bool tx)
{
    if (payload == NULL || len < 240U) {
        return;
    }

    /* Parse DHCP options to find message type, server identifier */
    uint8_t  message_type = 0U;
    uint32_t server_id    = 0U;
    uint32_t dns_primary  = 0U;
    uint32_t dns_secondary = 0U;
    uint16_t pos = 240U;
    while (pos < len) {
        uint8_t option = payload[pos++];
        if (option == 0U) {
            continue; /* padding */
        }
        if (option == 255U || pos >= len) {
            break; /* end */
        }
        uint8_t option_len = payload[pos++];
        if ((uint16_t)(pos + option_len) > len) {
            break;
        }
        if (option == 53U && option_len >= 1U) {
            message_type = payload[pos];
        }
        if (option == 54U && option_len >= 4U) {
            server_id = wifi_netif_be32(&payload[pos]);
        }
        if (option == 6U && option_len >= 4U) {
            dns_primary = wifi_netif_be32(&payload[pos]);
            if (option_len >= 8U) {
                dns_secondary = wifi_netif_be32(&payload[pos + 4U]);
            }
        }
        pos = (uint16_t)(pos + option_len);
    }

    uint32_t frame_xid = wifi_netif_be32(&payload[4]);
    uint32_t yiaddr    = wifi_netif_be32(&payload[16]);

    static const char * const dhcp_type_names[] = {
        "?", "DISCOVER", "OFFER", "REQUEST", "DECLINE", "ACK", "NACK", "RELEASE", "INFORM"
    };
    const char *type_str = (message_type < 9U) ? dhcp_type_names[message_type] : "?";
    const char *dir_str  = tx ? "TX" : "RX";

    /* Get lwIP's current xid for comparison (RX path only) */
    uint32_t lwip_xid = 0U;
    uint8_t  lwip_dhcp_state = 0U;
    if (!tx && g_wifi_netif != NULL) {
        struct dhcp *d = netif_dhcp_data(g_wifi_netif);
        if (d != NULL) {
            lwip_xid       = d->xid;
            lwip_dhcp_state = (uint8_t)d->state;
        }
    }

    LOGD(TAG,
         "DHCP %s type=%u(%s) xid=0x%08lx lwip_xid=0x%08lx %s "
         "yiaddr=%lu.%lu.%lu.%lu server=%lu.%lu.%lu.%lu lwip_state=%u",
         dir_str,
         (unsigned)message_type, type_str,
         (unsigned long)frame_xid,
         (unsigned long)lwip_xid,
         (frame_xid == lwip_xid || tx) ? "XID_OK" : "XID_MISMATCH!",
         (unsigned long)((yiaddr >> 24) & 0xFFU),
         (unsigned long)((yiaddr >> 16) & 0xFFU),
         (unsigned long)((yiaddr >>  8) & 0xFFU),
         (unsigned long)( yiaddr        & 0xFFU),
         (unsigned long)((server_id >> 24) & 0xFFU),
         (unsigned long)((server_id >> 16) & 0xFFU),
         (unsigned long)((server_id >>  8) & 0xFFU),
         (unsigned long)( server_id        & 0xFFU),
         (unsigned)lwip_dhcp_state);

    if (tx) {
        g_wifi_diag.dhcp_tx++;
    } else {
        g_wifi_diag.dhcp_rx++;
    }
    g_wifi_diag.dhcp_last_xid = frame_xid;
    switch (message_type) {
        case 1U: g_wifi_diag.dhcp_discover++; break;
        case 2U: g_wifi_diag.dhcp_offer++;    break;
        case 3U: g_wifi_diag.dhcp_request++;  break;
        case 5U: g_wifi_diag.dhcp_ack++;      break;
        case 6U: g_wifi_diag.dhcp_nack++;     break;
        default: break;
    }
    if (!tx && message_type == 5U) {
        /* DHCP options are encoded in network byte order, while lwIP stores
         * ip4_addr_t::addr in its native lwIP representation. Keep the
         * conversion at this boundary; otherwise 192.168.110.1 becomes
         * 1.110.168.192 when dns_setserver() formats it. */
        g_wifi_dns_primary = lwip_htonl(dns_primary);
        g_wifi_dns_secondary = lwip_htonl(dns_secondary != 0U
                                          ? dns_secondary : dns_primary);
    }
}

static uint16_t wifi_netif_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void wifi_netif_log_dhcp_frame(const uint8_t *frame, uint16_t len,
                                      const char *direction)
{
    if (frame == NULL || len < 14U + 20U + 8U) {
        return;
    }
    if (wifi_netif_be16(&frame[12]) != 0x0800U) {
        return;
    }

    uint8_t ihl = (uint8_t)((frame[14] & 0x0FU) * 4U);
    if (ihl < 20U || (uint16_t)(14U + ihl + 8U) > len ||
        frame[23] != 17U) {
        return;
    }

    uint16_t udp = (uint16_t)(14U + ihl);
    uint16_t src_port = wifi_netif_be16(&frame[udp]);
    uint16_t dst_port = wifi_netif_be16(&frame[udp + 2U]);
    char src_ip[IP4ADDR_STRLEN_MAX];
    char dst_ip[IP4ADDR_STRLEN_MAX];
    ip4_addr_t src_addr = {.addr = wifi_netif_be32(&frame[26])};
    ip4_addr_t dst_addr = {.addr = wifi_netif_be32(&frame[30])};
    if (!((src_port == 67U || src_port == 68U) &&
          (dst_port == 67U || dst_port == 68U))) {
        return;
    }

    /* Log src/dst MAC for frame origin verification */
    ip4addr_ntoa_r(&src_addr, src_ip, sizeof(src_ip));
    ip4addr_ntoa_r(&dst_addr, dst_ip, sizeof(dst_ip));
    LOGD(TAG,
         "DHCP frame %s len=%u src_mac=%02x:%02x:%02x:%02x:%02x:%02x "
         "dst_mac=%02x:%02x:%02x:%02x:%02x:%02x ip=%s->%s port=%u->%u",
         direction, (unsigned)len,
         frame[6], frame[7], frame[8], frame[9], frame[10], frame[11],
         frame[0], frame[1], frame[2], frame[3], frame[4], frame[5],
         src_ip, dst_ip,
         (unsigned)src_port, (unsigned)dst_port);

    uint16_t dhcp_off = (uint16_t)(udp + 8U);
    const uint8_t *payload = &frame[dhcp_off];
    uint16_t payload_len = (uint16_t)(len - dhcp_off);
    wifi_netif_record_dhcp(payload, payload_len,
                           (direction[0] == 'T'));
}

typedef struct {
    ip_addr_t primary;
    ip_addr_t secondary;
} wifi_dns_update_t;

static void wifi_netif_set_dns_cb(void *arg)
{
    const wifi_dns_update_t *dns = (const wifi_dns_update_t *)arg;
    dns_setserver(0, &dns->primary);
    dns_setserver(1, &dns->secondary);
}

static err_t wifi_netif_set_dns(const network_ip_config_t *config)
{
    wifi_dns_update_t dns = {
        .primary = {.addr = config->dns_primary},
        .secondary = {.addr = config->dns_secondary},
    };
    return tcpip_callback_with_block(wifi_netif_set_dns_cb, &dns, 1);
}

static int wifi_netif_ip_is_zero(void)
{
    return g_wifi_netif == NULL || ip4_addr_isany_val(*netif_ip4_addr(g_wifi_netif));
}

static void wifi_netif_status_cb(struct netif *netif) {
    if (netif == NULL) {
        return;
    }
    /* Some hotspots/repeaters omit DHCP option 3 (router) from their offer,
     * leaving gw=0.0.0.0 so the lease can never route off-subnet even though
     * the AP itself NATs at the offered address.  When a genuine DHCP lease
     * carries no router but does carry a DNS server, adopt that server as
     * the default gateway (same heuristic as Android/ESP-IDF).  A truly dead
     * path is still caught later by the manager reachability probe. */
    if (!ip4_addr_isany_val(*netif_ip4_addr(netif)) &&
        ip4_addr_isany_val(*netif_ip4_gw(netif)) &&
        g_wifi_dns_primary != 0U &&
        dhcp_supplied_address(netif)) {
        ip4_addr_t cur_ip = *netif_ip4_addr(netif);
        ip4_addr_t cur_mask = *netif_ip4_netmask(netif);
        ip4_addr_t fb_gw;
        fb_gw.addr = g_wifi_dns_primary; /* stored in ip_addr_t byte order */
        netif_set_addr(netif, &cur_ip, &cur_mask, &fb_gw);
        LOGI(TAG, "[GW-FIX] DHCP offer missing router option; "
                  "using DNS server as gateway");
    }
    char ip_buf[IP4ADDR_STRLEN_MAX];
    char mask_buf[IP4ADDR_STRLEN_MAX];
    char gw_buf[IP4ADDR_STRLEN_MAX];
    ip4addr_ntoa_r(netif_ip4_addr(netif), ip_buf, sizeof(ip_buf));
    ip4addr_ntoa_r(netif_ip4_netmask(netif), mask_buf, sizeof(mask_buf));
    ip4addr_ntoa_r(netif_ip4_gw(netif), gw_buf, sizeof(gw_buf));
    /* This callback runs after the deferred link/IP operation completed, so
     * it is the single authoritative WiFi netif snapshot. */
    LOGI(TAG, "Netif status final: up=%d link=%d IP=%s mask=%s gw=%s",
         netif_is_up(netif), netif_is_link_up(netif),
         ip_buf, mask_buf, gw_buf);
    if (!ip4_addr_isany_val(*netif_ip4_addr(netif))) {
        LOGI(TAG, "DHCP BOUND: IP=%s mask=%s gateway=%s",
             ip_buf, mask_buf, gw_buf);
        /* The ST67 event stream is not guaranteed to include GOT_IP.  Use
         * the completed lwIP/netif state as the source of truth for the
         * application-level event. */
        wifi_driver_notify_netif_got_ip();
    }
}

static const char *wifi_dhcp_state_name(uint8_t state)
{
    switch (state) {
        case DHCP_STATE_OFF: return "OFF";
        case DHCP_STATE_REQUESTING: return "REQUESTING";
        case DHCP_STATE_INIT: return "INIT";
        case DHCP_STATE_REBOOTING: return "REBOOTING";
        case DHCP_STATE_REBINDING: return "REBINDING";
        case DHCP_STATE_RENEWING: return "RENEWING";
        case DHCP_STATE_SELECTING: return "SELECTING";
        case DHCP_STATE_BOUND: return "BOUND";
        case DHCP_STATE_BACKING_OFF: return "BACKING_OFF";
        default: return "UNKNOWN";
    }
}

/* Forward declarations */
static void netif_sta_notify_callback(void *arg);
static void netif_ap_notify_callback(void *arg);
static int32_t netif_link_sta_up_cb(void);
static int32_t netif_link_sta_down_cb(void);
static int32_t netif_link_ap_up_cb(void);
static int32_t netif_link_ap_down_cb(void);

static const st67w611_lwip_callbacks_t g_lwip_callbacks = {
    .link_sta_up = netif_link_sta_up_cb,
    .link_sta_down = netif_link_sta_down_cb,
    .link_ap_up = netif_link_ap_up_cb,
    .link_ap_down = netif_link_ap_down_cb,
    .rx_sta_notify = netif_sta_notify_callback,
    .rx_ap_notify = netif_ap_notify_callback,
};
static void netif_rx_task(void *arg);
static struct pbuf_custom *netif_pbuf_alloc(void *buffer);
static void netif_pbuf_free(struct pbuf *pb);

static void wifi_netif_process_flush_request(void)
{
    if (!g_rx_flush_requested) return;
    int32_t count = st67w611_lwip_flush_rx();
    g_rx_flush_requested = false;
    g_rx_flush_pending = false;
    if (count > 0) {
        LOGI(TAG, "Flushed %ld stale WiFi RX packets", (long)count);
    }
    if (g_rx_flush_done != NULL) {
        (void)xSemaphoreGive(g_rx_flush_done);
    }
}

/* Registered notify callbacks for the st67w611_lwip adapter. */
static void netif_sta_notify_callback(void *arg) {
    (void)arg;
    g_wifi_diag.rx_notify++;
    if (g_rx_task_handle != NULL) {
        (void)xTaskNotify(g_rx_task_handle, NETIF_STA_RX_RDY, eSetBits);
    }
}

static void netif_ap_notify_callback(void *arg) {
    (void)arg;
    /* AP RX not used in STA mode; placeholder for future AP support */
}

/* -----------------------------------------------------------------------
 * Link-up deferred work: runs INSIDE tcpip thread.
 * Safe to call lwIP functions directly (netif_set_link_up, dhcp_start …).
 * Must NOT call netifapi_* from here (already in tcpip context → deadlock).
 * ----------------------------------------------------------------------- */
static void netif_link_sta_up_work(void *arg)
{
    (void)arg;
    if (g_wifi_netif == NULL) {
        return;
    }

    /* The disconnect callback requests an RX-queue boundary asynchronously.
     * Do not start a new DHCP transaction until the RX task has completed
     * that flush, otherwise the first DHCP frames can be lost after a fast
     * reconnect.  The RX task is independent from tcpip, so this bounded
     * wait cannot form the old modem-RX/tcpip deadlock. */
    if (wifi_netif_wait_rx_flush(100U) != 0) {
        LOGW(TAG, "RX flush not confirmed before WiFi link UP");
    }

    /* Mark the link as UP (we are already in tcpip thread). */
    netif_set_link_up(g_wifi_netif);

    /* Read DHCP / static-IP configuration and apply it. */
    network_config_t net_cfg;
    if (network_manager_get_config(&net_cfg) != 0) {
        LOGE(TAG, "Unable to read WiFi network configuration");
        return;
    }

#if (LWIP_DHCP == 1)
    if (net_cfg.wifi.dhcp) {
        ip4_addr_t zero = {0};
        /* Stop any previous DHCP session and clear the address. */
        dhcp_release_and_stop(g_wifi_netif);
        netif_set_addr(g_wifi_netif, &zero, &zero, &zero);
        err_t err = dhcp_start(g_wifi_netif);
        LOGI(TAG, "DHCP start: %d", (int)err);
        return;
    }
#endif

    /* Static IP path */
    if (net_cfg.wifi.static_ip.ip == 0U ||
        net_cfg.wifi.static_ip.netmask == 0U ||
        net_cfg.wifi.static_ip.gateway == 0U) {
        LOGE(TAG, "Invalid static IP configuration");
        return;
    }
    ip4_addr_t ip   = {.addr = net_cfg.wifi.static_ip.ip};
    ip4_addr_t mask = {.addr = net_cfg.wifi.static_ip.netmask};
    ip4_addr_t gw   = {.addr = net_cfg.wifi.static_ip.gateway};
    dhcp_release_and_stop(g_wifi_netif);
    netif_set_addr(g_wifi_netif, &ip, &mask, &gw);
    LOGI(TAG, "Static IP applied");
}

/* Link callbacks - called from W6X WiFi event task (W61_MDM_RX_TASK_PRIO).
 *
 * IMPORTANT: This task also owns the SPI channel.  Any blocking netifapi_*
 * call here risks deadlock:
 *   tcpip thread wakes → triggers lwIP TX (ARP/DHCP) → needs SPI
 *   → SPI needs modem-RX task → modem-RX task is blocked on netifapi
 *
 * Solution: post work to tcpip thread via tcpip_callback and return
 * IMMEDIATELY so the modem-RX task stays free.
 */
static int32_t netif_link_sta_up_cb(void) {
    LOGI(TAG, "STA link UP requested");
    if (g_wifi_netif == NULL) {
        return -1;
    }
    err_t err = tcpip_callback(netif_link_sta_up_work, NULL);
    if (err != ERR_OK) {
        LOGE(TAG, "tcpip_callback (link up) failed: %d", (int)err);
        return -1;
    }
    return 0;  /* Return immediately — do NOT block the modem-RX task */
}

/* -----------------------------------------------------------------------
 * Link-down deferred work: runs INSIDE tcpip thread.
 * ----------------------------------------------------------------------- */
static void netif_link_sta_down_work(void *arg)
{
    (void)arg;
    if (g_wifi_netif == NULL) {
        return;
    }
#if (LWIP_DHCP == 1)
    dhcp_release_and_stop(g_wifi_netif);
#endif
    ip4_addr_t zero = {0};
    netif_set_addr(g_wifi_netif, &zero, &zero, &zero);
    netif_set_link_down(g_wifi_netif);
    wifi_netif_request_rx_flush();
    LOGI(TAG, "Netif STA link DOWN applied");
}

static int32_t netif_link_sta_down_cb(void) {
    LOGI(TAG, "STA link DOWN requested");
    if (g_wifi_netif == NULL) {
        return -1;
    }
    err_t err = tcpip_callback(netif_link_sta_down_work, NULL);
    if (err != ERR_OK) {
        LOGE(TAG, "tcpip_callback (link down) failed: %d", (int)err);
    }
    return 0;  /* Return immediately */
}

static int32_t netif_link_ap_up_cb(void) {
    (void)TAG;
    /* AP mode not implemented */
    return 0;
}

static int32_t netif_link_ap_down_cb(void) {
    /* AP mode not implemented */
    return 0;
}

/* Custom pbuf allocation */
static struct pbuf_custom *netif_pbuf_alloc(void *buffer) {
    netif_pbuf_t *netif_pbuf = pvPortMalloc(sizeof(netif_pbuf_t));
    if (netif_pbuf == NULL) {
        LOGE(TAG, "pbuf allocation failed");
        return NULL;
    }
    (void)memset(netif_pbuf, 0, sizeof(netif_pbuf_t));
    netif_pbuf->pb.custom_free_function = netif_pbuf_free;
    netif_pbuf->buffer = buffer;
    return &netif_pbuf->pb;
}

/* Custom pbuf free - called by lwIP when pbuf refcount reaches 0 */
static void netif_pbuf_free(struct pbuf *pb) {
    if (pb == NULL) {
        return;
    }
    struct pbuf_custom *pbufc = (struct pbuf_custom *)pb;
    netif_pbuf_t *netif_pbuf = (netif_pbuf_t *)((uint8_t *)pbufc - offsetof(netif_pbuf_t, pb));
    if (netif_pbuf->buffer != NULL) {
        (void)st67w611_lwip_free(netif_pbuf->buffer);
    }
    vPortFree(netif_pbuf);
}

/* RX processing - drain all available packets from STA queue */
static int32_t netif_rx_process(void) {
    int32_t ret;
    int32_t packet_len;
    struct pbuf *pb;
    void *buffer = NULL;
    uint8_t *payload = NULL;

    if (g_wifi_netif == NULL) {
        return -1;
    }

    /* Get the next received packet from the ST67W611 module */
    ret = st67w611_lwip_input(&buffer, &payload);
    if (ret <= 0) {
        g_wifi_diag.rx_empty++;
        if (buffer != NULL) {
            (void)st67w611_lwip_free(buffer);
        }
        return ret;
    }
    packet_len = ret;
    wifi_netif_log_dhcp_frame(payload, (uint16_t)packet_len, "RX");

    /* Wrap driver buffer in a custom pbuf */
    struct pbuf_custom *pbufc = netif_pbuf_alloc(buffer);
    if (pbufc == NULL) {
        LOGE(TAG, "netif_pbuf_alloc failed");
        (void)st67w611_lwip_free(buffer);
        return -1;
    }

    /* Create pbuf wrapping the driver payload */
    pb = pbuf_alloced_custom(PBUF_RAW, ret, PBUF_REF, pbufc, payload, ret);
    if (pb == NULL) {
        LOGE(TAG, "pbuf_alloced_custom failed");
        (void)st67w611_lwip_free(buffer);
        vPortFree(pbufc);
        return -1;
    }

    /* Pass packet up to lwIP.
     * On ERR_OK: lwIP took ownership; custom free (netif_pbuf_free) will call
     *            the st67w611_lwip adapter when refcount drops to 0.
     * On error:  lwIP did NOT take ownership; free the container via
     *            netif_pbuf_free which frees both the pbuf wrapper AND buffer. */

    /* Log netif state immediately before handing to lwIP */
    if (g_wifi_netif != NULL) {
        LOGD(TAG, "netif_input: up=%d link=%d flags=0x%02x len=%d",
             netif_is_up(g_wifi_netif),
             netif_is_link_up(g_wifi_netif),
             (unsigned)g_wifi_netif->flags,
             packet_len);
    }

    ret = g_wifi_netif->input(pb, g_wifi_netif);
    {
        struct dhcp *dhcp = netif_dhcp_data(g_wifi_netif);
        uint8_t state = dhcp != NULL ? (uint8_t)dhcp->state : DHCP_STATE_OFF;
        char ip_buf[IP4ADDR_STRLEN_MAX];
        ip4addr_ntoa_r(netif_ip4_addr(g_wifi_netif), ip_buf, sizeof(ip_buf));
        LOGD(TAG, "DHCP input result: err=%ld state=%s(%u) IP=%s",
             (long)ret, wifi_dhcp_state_name(state), (unsigned)state, ip_buf);
    }
    if (ret != ERR_OK) {
        g_wifi_diag.rx_input_errors++;
        LOGE(TAG, "netif input error: %d (up=%d link=%d)",
             ret,
             netif_is_up(g_wifi_netif),
             netif_is_link_up(g_wifi_netif));
        netif_pbuf_free(pb);
        return -1;
    }

    g_wifi_diag.rx_packets++;
    LOGD(TAG, "RX packet accepted by lwIP: len=%ld count=%lu",
         (long)packet_len, (unsigned long)g_wifi_diag.rx_packets);

    return ret;
}

/* RX task - processes packets from ST67W611 */
static void netif_rx_task(void *arg) {
    (void)arg;
    uint32_t notified;

    LOGI(TAG, "WiFi netif RX task started");

    while (1) {
        BaseType_t got_notify = xTaskNotifyWait(0, NETIF_STA_RX_RDY, &notified,
                                                 pdMS_TO_TICKS(WIFI_NETIF_RX_POLL_MS));
        if (got_notify == pdFALSE) {
            LOGD(TAG, "RX poll timeout");
        }

        wifi_netif_process_flush_request();

        /* Drain a bounded number of packets.  The ST67 RX task has a high
         * priority because it owns the modem transport, but it must never
         * monopolise the scheduler when the vendor queue keeps reporting
         * packets after a WiFi error/disconnect. */
        uint32_t processed = 0U;
        while (processed < WIFI_NETIF_RX_BUDGET && netif_rx_process() > 0) {
            processed++;
        }
        if (processed == WIFI_NETIF_RX_BUDGET) {
            /* Delay by one tick rather than only yielding: lower-priority
             * HTTP, shell, monitor and network tasks must get CPU time even
             * when the RX notification remains asserted continuously. */
            vTaskDelay(1U);
        }
    }
}

/* Linkoutput callback - called by lwIP to send a packet */
err_t wifi_netif_output(struct netif *netif, struct pbuf *p) {
    (void)netif;
    if (p == NULL || p->tot_len == 0) {
        return ERR_OK;
    }

    int32_t ret;
    static uint8_t tx_buffer[1514];
    if (p->next == NULL) {
        /* Single contiguous pbuf */
        ret = st67w611_lwip_output((const uint8_t *)p->payload, p->len);
    } else {
        /* Chained pbuf: copy into contiguous buffer first */
        if (p->tot_len > sizeof(tx_buffer)) {
            LOGE(TAG, "TX packet size %u exceeds buffer limit", p->tot_len);
            return ERR_BUF;
        }
        (void)pbuf_copy_partial(p, tx_buffer, p->tot_len, 0);
        ret = st67w611_lwip_output(tx_buffer, p->tot_len);
    }

    if (p->next == NULL) {
        /* DHCP accounting is performed only for recognized DHCP frames. */
        wifi_netif_log_dhcp_frame((const uint8_t *)p->payload,
                                  p->len, "TX");
    } else {
        wifi_netif_log_dhcp_frame(tx_buffer, (uint16_t)p->tot_len, "TX");
    }

    if (ret < 0) {
        g_wifi_diag.tx_errors++;
        LOGE(TAG, "ST67 lwIP output failed: %ld", (int32_t)ret);
        switch (ret) {
            case -1:  return ERR_BUF;
            case -2:  return ERR_VAL;
            case -3:  return ERR_MEM;
            case -4:  return ERR_BUF;
            case -5:  return ERR_INPROGRESS;
            default:  return ERR_VAL;
        }
    }

    g_wifi_diag.tx_packets++;
    LOGD(TAG, "TX packet len=%u count=%lu",
         (unsigned)p->tot_len, (unsigned long)g_wifi_diag.tx_packets);

    return ERR_OK;
}

/* Initialize W6X_Netif callbacks and create RX task */
int32_t wifi_netif_init(void) {
    /* Register STA/AP RX handlers and link callbacks with W6X library */
    if (st67w611_lwip_attach(g_wifi_netif, &g_lwip_callbacks) != ST67W611_OK) {
        LOGE(TAG, "st67w611_lwip_attach failed");
        return -1;
    }
    g_wifi_diag.transport_bound = 1U;

    g_rx_flush_done = xSemaphoreCreateBinary();
    if (g_rx_flush_done == NULL) {
        LOGE(TAG, "Failed to create RX flush semaphore");
        return -1;
    }

    /* Create RX processing task */
    BaseType_t xret = xTaskCreate(netif_rx_task,
                                  "WiFiNetif",
                                  WIFI_NETIF_TASK_STACK,
                                  NULL,
                                  WIFI_NETIF_TASK_PRIO,
                                  &g_rx_task_handle);
    if (xret != pdPASS) {
        (void)st67w611_lwip_detach(g_wifi_netif);
        LOGE(TAG, "Failed to create WiFi netif RX task");
        return -1;
    }

    LOGI(TAG, "WiFi netif adapter initialized");
    return 0;
}

void wifi_netif_set_netif(struct netif *nif) {
    g_wifi_netif = nif;
}

void wifi_netif_set_status_callback(void) {
    if (g_wifi_netif != NULL) {
        netif_set_status_callback(g_wifi_netif, wifi_netif_status_cb);
    }
}

int32_t wifi_netif_apply_config(const network_config_t *config)
{
    if (g_wifi_netif == NULL || config == NULL) {
        return -1;
    }

#if (LWIP_DHCP == 1)
    if (config->wifi.dhcp) {
        ip4_addr_t zero = {0};
        (void)netifapi_dhcp_release_and_stop(g_wifi_netif);
        err_t clear_err = netifapi_netif_set_addr(g_wifi_netif, &zero, &zero, &zero);
        err_t start_err = netifapi_dhcp_start(g_wifi_netif);
        LOGI(TAG, "DHCP start: clear=%d start=%d", clear_err, start_err);
        return start_err == ERR_OK ? 0 : -1;
    }
#endif

    if (config->wifi.static_ip.ip == 0U ||
        config->wifi.static_ip.netmask == 0U ||
        config->wifi.static_ip.gateway == 0U) {
        LOGE(TAG, "Invalid WiFi static IP configuration");
        return -1;
    }

    ip4_addr_t ip = {.addr = config->wifi.static_ip.ip};
    ip4_addr_t mask = {.addr = config->wifi.static_ip.netmask};
    ip4_addr_t gw = {.addr = config->wifi.static_ip.gateway};
    (void)netifapi_dhcp_release_and_stop(g_wifi_netif);
    err_t set_err = netifapi_netif_set_addr(g_wifi_netif, &ip, &mask, &gw);
    err_t up_err = netifapi_netif_set_up(g_wifi_netif);

    err_t dns_err = wifi_netif_set_dns(&config->wifi.static_ip);
    char ip_buf[IP4ADDR_STRLEN_MAX];
    char mask_buf[IP4ADDR_STRLEN_MAX];
    char gw_buf[IP4ADDR_STRLEN_MAX];
    ip4addr_ntoa_r(&ip, ip_buf, sizeof(ip_buf));
    ip4addr_ntoa_r(&mask, mask_buf, sizeof(mask_buf));
    ip4addr_ntoa_r(&gw, gw_buf, sizeof(gw_buf));
    LOGI(TAG, "Static IP applied: %s mask=%s gw=%s",
         ip_buf, mask_buf, gw_buf);
    return (set_err == ERR_OK && up_err == ERR_OK && dns_err == ERR_OK) ? 0 : -1;
}

int wifi_netif_has_ip(void)
{
    return !wifi_netif_ip_is_zero();
}

int wifi_netif_restart_dhcp(void)
{
    if (g_wifi_netif == NULL || !netif_is_link_up(g_wifi_netif)) return -1;
    (void)netifapi_dhcp_release_and_stop(g_wifi_netif);
    wifi_netif_request_rx_flush();
    (void)wifi_netif_wait_rx_flush(100U);
    err_t err = netifapi_dhcp_start(g_wifi_netif);
    LOGI(TAG, "DHCP restart: start=%d", (int)err);
    return err == ERR_OK ? 0 : -1;
}

void wifi_netif_request_rx_flush(void)
{
    /* A binary semaphore may still contain the completion from an earlier
     * flush. Drain it before publishing a new request so a waiter cannot
     * mistake an old completion for the current RX queue boundary. */
    if (g_rx_flush_done != NULL) {
        (void)xSemaphoreTake(g_rx_flush_done, 0U);
    }
    g_rx_flush_pending = true;
    g_rx_flush_requested = true;
    if (g_rx_task_handle != NULL) {
        (void)xTaskNotify(g_rx_task_handle, NETIF_STA_RX_RDY, eSetBits);
    }
}

int wifi_netif_wait_rx_flush(uint32_t timeout_ms)
{
    if (g_rx_flush_done == NULL) return -1;
    /* Initial boot has no flush request.  Do not wait for a semaphore
     * completion that cannot exist; only reconnect paths need the barrier. */
    if (!g_rx_flush_pending) return 0;
    return xSemaphoreTake(g_rx_flush_done, pdMS_TO_TICKS(timeout_ms)) == pdTRUE ? 0 : -1;
}

void wifi_netif_get_diag(wifi_netif_diag_t *diag)
{
    st67w611_lwip_transport_diagnostics_t spi_stats;
    if (diag == NULL) {
        return;
    }
    *diag = g_wifi_diag;
    diag->transport_bound = (uint8_t)(st67w611_lwip_is_bound() ? 1U : 0U);
    if (st67w611_lwip_get_transport_diagnostics(&spi_stats) == 0) {
        diag->spi_rx_packets = spi_stats.rx_packets;
        diag->spi_rx_drops = spi_stats.rx_drops;
        diag->spi_io_errors = spi_stats.io_errors;
        diag->spi_header_errors = spi_stats.header_errors;
        diag->spi_rx_stalls = spi_stats.rx_stalls;
    }
    if (g_wifi_netif != NULL && netif_dhcp_data(g_wifi_netif) != NULL) {
        struct dhcp *d = netif_dhcp_data(g_wifi_netif);
        diag->dhcp_state    = (uint8_t)d->state;
        diag->dhcp_lwip_xid = d->xid;
    } else {
        diag->dhcp_state    = 0U;
        diag->dhcp_lwip_xid = 0U;
    }
}

void wifi_netif_dump_spi_queues(void)
{
    st67w611_lwip_dump_transport_queues();
}

void wifi_netif_transport_invalidated(void)
{
    st67w611_lwip_transport_invalidated();
    g_wifi_diag.transport_bound = 0U;
}

int32_t wifi_netif_rebind_after_recovery(void)
{
    st67w611_status_t status;
    LOGI(TAG, "T02 netif rebind started");
    status = st67w611_lwip_rebind();
    if (status != ST67W611_OK) {
        g_wifi_diag.transport_rebind_failures++;
        g_wifi_diag.transport_bound = 0U;
        LOGE(TAG, "T02 netif rebind failed: %d", status);
        return -1;
    }
    g_wifi_diag.transport_rebinds++;
    g_wifi_diag.transport_bound = 1U;
    LOGI(TAG, "T02 STA/AP queues rebound");
    return 0;
}

int wifi_netif_is_bound(void)
{
    return st67w611_lwip_is_bound();
}

void wifi_netif_get_dns(uint32_t *primary, uint32_t *secondary)
{
    if (primary != NULL) *primary = g_wifi_dns_primary;
    if (secondary != NULL) *secondary = g_wifi_dns_secondary;
}
