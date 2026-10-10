/**
 * @file wifi_driver.c
 * @brief WiFi (ST67W611) driver for Mission T01/T02
 *
 * T01 owns Wi-Fi networking inside ST67W611. T02 additionally exposes a
 * host-side lwIP netif through the W6X_Netif adapter.
 *
 * Hardware connections (see Documents/Hardware/02_PINOUT_AND_INTERFACES.md):
 * - SPI1: PB3 (SCK), PB4 (MISO), PB5 (MOSI)
 * - GPIO: PD7 (CS), PD0 (RDY), PD1 (EN), PD2 (BOOT), PD3 (Wakeup), PD4 (32K)
 */
#include "wifi_driver.h"
#if (ST67_ARCH == 2)
#include "wifi_netif.h"
#include "usb_netif.h"
#include "st67w611_lwip.h"
#include "st67w611_lwip_internal.h"
#endif
#include "logger.h"
#include "st67w611.h"
#include "st67w611_port.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#if (ST67_ARCH == 2)
#include "lwip/netif.h"
#include "lwip/ip_addr.h"
#include "lwip/dhcp.h"
#include "lwip/tcpip.h"
#include "lwip/etharp.h"
#include "lwip/netifapi.h"
#endif
#include <string.h>

#include "logging.h"
#include "shell_serial.h"

static const char *TAG = "wifi_driver";

#define WIFI_VENDOR_LOG_LINE_SIZE 256U
#define WIFI_VENDOR_LOG_FLUSH_MS  100U
static QueueHandle_t s_vendor_log_queue = NULL;
static TaskHandle_t s_vendor_log_task = NULL;
static volatile uint32_t s_vendor_log_dropped = 0U;

typedef struct {
    uint16_t length;
    char data[WIFI_VENDOR_LOG_LINE_SIZE];
} vendor_log_chunk_t;

static void vendor_log_task(void *arg)
{
    vendor_log_chunk_t chunk;
    char line[WIFI_VENDOR_LOG_LINE_SIZE];
    char frame[WIFI_VENDOR_LOG_LINE_SIZE + 2U];
    size_t line_len = 0U;
    (void)arg;
    for (;;) {
        if (xQueueReceive(s_vendor_log_queue, &chunk,
                          pdMS_TO_TICKS(WIFI_VENDOR_LOG_FLUSH_MS)) == pdTRUE) {
            for (uint16_t i = 0U; i < chunk.length; ++i) {
                char ch = chunk.data[i];
                if (ch == '\r' || ch == '\n') {
                    if (line_len > 0U) {
                        memcpy(frame, line, line_len);
                        frame[line_len] = '\r';
                        frame[line_len + 1U] = '\n';
                        serial_tx_write((const uint8_t *)frame,
                                        (uint16_t)(line_len + 2U), 1000U);
                        line_len = 0U;
                    }
                } else if (line_len < sizeof(line) - 1U) {
                    line[line_len++] = ch;
                }
            }
        } else if (line_len > 0U) {
            memcpy(frame, line, line_len);
            frame[line_len] = '\r';
            frame[line_len + 1U] = '\n';
            serial_tx_write((const uint8_t *)frame,
                            (uint16_t)(line_len + 2U), 1000U);
            line_len = 0U;
        }
    }
}

/* Configuration */
#define WIFI_TASK_STACK_SIZE    2048
#define WIFI_TASK_PRIO         8

/* Static netif allocation */
#if (ST67_ARCH == 2)
static struct netif wifi_netif;
#endif
#if (ST67_ARCH == 2)
static uint8_t wifi_mac_addr[6] = {0};
static const st67w611_capabilities_t wifi_capabilities = {
    .get_ip = st67w611_lwip_get_ip_capability,
    .ping = st67w611_lwip_ping_capability,
    .context = &wifi_netif
};
#endif

/* WiFi state is defined in wifi_driver.h */
static wifi_state_t wifi_state = WIFI_STATE_IDLE;
static uint8_t wifi_connected = 0;
static uint8_t wifi_initialized = 0;
static wifi_event_cb_t g_wifi_cb = NULL;
static TaskHandle_t wifi_startup_task_handle = NULL;
static TaskHandle_t wifi_task_handle = NULL;
static uint8_t s_consecutive_connect_failures;

#define WIFI_SOFT_CONNECT_FAILURE_LIMIT 2U
#define WIFI_POST_RESET_SETTLE_MS       750U

#if (ST67_ARCH == 2)
static st67w611_status_t wifi_before_transport_restart(void *context)
{
    (void)context;
    wifi_netif_request_rx_flush();
    if (wifi_netif_wait_rx_flush(500U) != 0) {
        LOGW(TAG, "RX flush timed out before ST67 transport restart");
    }
    wifi_netif_transport_invalidated();
    return ST67W611_OK;
}

static st67w611_status_t wifi_after_transport_restart(void *context)
{
    (void)context;
    return wifi_netif_rebind_after_recovery() == 0
               ? ST67W611_OK
               : ST67W611_ERR_VENDOR;
}

static const st67w611_lifecycle_hooks_t wifi_lifecycle_hooks = {
    .before_transport_restart = wifi_before_transport_restart,
    .after_transport_restart = wifi_after_transport_restart,
    .context = NULL,
};
#endif

static int wifi_driver_recover_st67(void)
{
    st67w611_state_t state = ST67W611_STATE_OFF;
    st67w611_status_t status;

    if (st67w611_get_state(&state) != ST67W611_OK) {
        return -1;
    }
    if (state == ST67W611_STATE_READY) {
        return 0;
    }
    if (state == ST67W611_STATE_OFF ||
        state == ST67W611_STATE_INITIALIZING ||
        state == ST67W611_STATE_RESETTING) {
        return -1;
    }

    LOGW(TAG, "Recovering ST67 from state=%d", state);
    status = st67w611_reset();
    if (status != ST67W611_OK) {
        LOGE(TAG, "ST67 recovery failed: %d", status);
        wifi_state = WIFI_STATE_ERROR;
        wifi_connected = 0U;
        return -1;
    }

    if (st67w611_get_state(&state) != ST67W611_OK ||
        state != ST67W611_STATE_READY) {
        LOGE(TAG, "ST67 recovery did not reach READY: state=%d", state);
        wifi_state = WIFI_STATE_ERROR;
        wifi_connected = 0U;
        return -1;
    }
    LOGI(TAG, "ST67 recovery completed: READY");
    vTaskDelay(pdMS_TO_TICKS(WIFI_POST_RESET_SETTLE_MS));
    s_consecutive_connect_failures = 0U;
    wifi_state = WIFI_STATE_INIT;
    wifi_connected = 0U;
    return 0;
}

/* Vendor logging is queued by its own OutputTask.  It must be initialized
 * before W6X_Init(), otherwise the first vendor diagnostic calls
 * xQueueSendToBack() with a NULL LoggingQueue. */
static void st67_vendor_log_output(const char *message)
{
    vendor_log_chunk_t chunk;
    size_t length;
    if (message == NULL || s_vendor_log_queue == NULL) return;
    length = strlen(message);
    while (length > 0U) {
        uint16_t part = (uint16_t)((length > sizeof(chunk.data)) ? sizeof(chunk.data) : length);
        memcpy(chunk.data, message, part);
        chunk.length = part;
        if (xQueueSend(s_vendor_log_queue, &chunk, 0U) != pdTRUE) {
            s_vendor_log_dropped++;
            break;
        }
        message += part;
        length -= part;
    }
}

uint32_t wifi_driver_vendor_log_dropped(void)
{
    return s_vendor_log_dropped;
}

/**
 * @brief Report errors raised by the ST67W6X driver.
 *
 * The vendor driver can report an error while W6X_Init() is still running,
 * so this callback must be registered before the first W6X API call.
 */
/* WiFi credentials from network manager */
static char wifi_ssid[33] = {0};
static char wifi_password[65] = {0};
static bool is_scanning = false;
static char wifi_scan_json[1024] = "[]";

bool wifi_driver_is_scanning(void) {
    return is_scanning;
}

const char* wifi_driver_get_scan_results_json(void) {
    return wifi_scan_json;
}

static void wifi_scan_result_handler(st67w611_status_t status,
                                     const st67w611_scan_result_t *entry,
                                     void *user_context)
{
    (void)user_context;
    if (status != ST67W611_OK) {
        is_scanning = false;
        LOGE(TAG, "WiFi scan failed: %d", status);
        snprintf(wifi_scan_json, sizeof(wifi_scan_json), "[]");
        return;
    }
    if (entry == NULL) {
        is_scanning = false;
        {
            size_t used = strlen(wifi_scan_json);
            if (used + 2U < sizeof(wifi_scan_json)) {
                wifi_scan_json[used] = ']';
                wifi_scan_json[used + 1U] = '\0';
            }
        }
        LOGI(TAG, "WiFi scan completed");
        return;
    }

    LOGI(TAG, "WiFi scan callback: count=1 ssid=\"%s\" rssi=%d channel=%u "
         "mac=%02X:%02X:%02X:%02X:%02X:%02X",
         entry->ssid,
         (int)entry->rssi,
         (unsigned)entry->channel,
         entry->mac[0], entry->mac[1], entry->mac[2],
         entry->mac[3], entry->mac[4], entry->mac[5]);

    {
        size_t used = strlen(wifi_scan_json);
        int written = snprintf(wifi_scan_json + used,
                               sizeof(wifi_scan_json) - used,
                               "%s{\"ssid\":\"%s\",\"rssi\":%d,"
                               "\"channel\":%u,\"mac\":\"%02X:%02X:%02X:%02X:%02X:%02X\"}",
                               used > 1U ? "," : "", entry->ssid,
                               (int)entry->rssi,
                               (unsigned)entry->channel,
                               entry->mac[0], entry->mac[1], entry->mac[2],
                               entry->mac[3], entry->mac[4], entry->mac[5]);
        if (written < 0 || (size_t)written >= sizeof(wifi_scan_json) - used) {
            is_scanning = false;
            return;
        }
    }
}

/**
 * @brief Get MAC address for WiFi from ST67W611 module
 * Uses the module's own MAC address for correctness and to avoid duplicates.
 */
#if (ST67_ARCH == 2)
static void get_mac_address(uint8_t *mac) {
    (void)st67w611_get_mac(mac);
}
#endif

/**
 * @brief WiFi event callback (called from ST67W611 library)
 */
static void wifi_event_handler(st67w611_event_t event_id, void *event_args, void *user_context) {
    (void)user_context;
    switch (event_id) {
        case ST67W611_EVENT_CONNECTED:
            LOGI(TAG, "WiFi connected to AP");
            wifi_state = WIFI_STATE_CONNECTED;
            wifi_connected = 1;
            s_consecutive_connect_failures = 0U;
            /* Link-up + DHCP start are handled by netif_link_sta_up_cb
             * (registered via W6X_Netif_Init) using thread-safe netifapi_*.
             * Do NOT call netif_set_link_up() directly here — it is not
             * thread-safe and races with the netifapi call, corrupting
             * lwIP state and causing a tcpip-thread deadlock. */
            if (g_wifi_cb) {
                g_wifi_cb((uint8_t)WIFI_STATE_CONNECTED);
            }
            break;

        case ST67W611_EVENT_DISCONNECTED:
            LOGI(TAG, "WiFi disconnected");
            wifi_state = WIFI_STATE_DISCONNECTED;
            wifi_connected = 0;
            /* Link-down is handled by netif_link_sta_down_cb via netifapi_*.
             * Do NOT call netif_set_link_down() directly here. */
            if (g_wifi_cb) {
                g_wifi_cb((uint8_t)WIFI_STATE_DISCONNECTED);
            }
            break;

        case ST67W611_EVENT_GOT_IP:
            LOGI(TAG, "WiFi got IP address");
            wifi_state = WIFI_STATE_GOT_IP;
            if (g_wifi_cb) {
                g_wifi_cb((uint8_t)WIFI_STATE_GOT_IP);
            }
            break;

        case ST67W611_EVENT_CONNECTING:
            LOGI(TAG, "WiFi connecting...");
            wifi_state = WIFI_STATE_CONNECTING;
            break;

        default:
            LOGD(TAG, "WiFi event: %d", (int)event_id);
            break;
    }
}

/**
 * @brief Initialize WiFi driver and register netif
 * @return T02 Wi-Fi netif, or NULL on failure. T01 also returns NULL on
 *         success because it has no host-side netif; inspect wifi state.
 */
#if (ST67_ARCH == 2)
static err_t wifi_netif_init_cb(struct netif *netif) {
    if (netif == NULL) return ERR_ARG;
    netif->name[0] = 'w';
    netif->name[1] = 'l';
    netif->output = etharp_output;
    /* wifi_netif_output is declared in wifi_netif.h */
    extern err_t wifi_netif_output(struct netif *netif, struct pbuf *p);
    netif->linkoutput = wifi_netif_output;
    netif->hwaddr_len = 6;
    netif->mtu = 1500;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_IGMP;
    return ERR_OK;
}
#endif

struct netif* wifi_driver_init(void) {
    /* Guard: if already initialized, return existing netif */
    if (wifi_initialized) {
#if (ST67_ARCH == 2)
        return &wifi_netif;
#else
        return NULL;
#endif
    }

#if (ST67_ARCH == 2)
    ip4_addr_t ipaddr, netmask, gw;
#endif

    wifi_connected = 0;
    wifi_initialized = 0;
    wifi_state = WIFI_STATE_INITIALIZING;
    LOGI(TAG, "WiFi (ST67W611) initializing...");

    if (s_vendor_log_queue == NULL) {
        s_vendor_log_queue = xQueueCreate(16U, sizeof(vendor_log_chunk_t));
        if (s_vendor_log_queue == NULL ||
            xTaskCreate(vendor_log_task, "VLOG", 768U, NULL, 5U,
                        &s_vendor_log_task) != pdPASS) {
            LOGE(TAG, "Failed to initialize vendor log pipeline");
            return NULL;
        }
    }

    if (vLoggingInit(st67_vendor_log_output) == NULL) {
        LOGE(TAG, "Failed to initialize ST67 vendor logging");
        wifi_state = WIFI_STATE_ERROR;
        return NULL;
    }
    LOGI(TAG, "ST67 vendor logging initialized");

    /* Step 1: Initialize SPI interface to ST67W611 module
     * This is done by the selected st67w611_port adapter.
     */

    /* Register callbacks before initialization. The ST reference application
     * does this before W6X_Init(), because initialization itself may report an
     * error through APP_error_cb. */
    st67w611_config_t st67_config = {
        .port_ops = &st67w611_port_ops,
        .port_context = NULL,
        .capabilities =
#if (ST67_ARCH == 2)
            &wifi_capabilities,
#else
            NULL,
#endif
        .event_cb = wifi_event_handler,
        .user_context = NULL,
        .lifecycle =
#if (ST67_ARCH == 2)
            &wifi_lifecycle_hooks,
#else
            NULL,
#endif
        .recovery = {
            /* Keep one vendor connect call per network-manager attempt.
             * Retry/backoff is owned by network_manager; retrying here also
             * retries association/vendor errors and hides the real failure. */
            .max_transport_retries = 0U,
            .retry_delay_ms = 0U,
            .reset_timeout_ms = 3000U
        }
    };

    /* Step 2: Initialize ST67W611 driver
     * W6X_Init() communicates with the module via SPI.
     * If the module is not responding (RDY still low), this can hang indefinitely
     * because the library's internal timeout may not be aggressive enough.
     * We wrap with a watchdog to ensure we don't freeze the shell. */
    LOGI(TAG, "Calling W6X_Init()...");
    uint32_t t0 = xTaskGetTickCount();
    st67w611_status_t status = st67w611_init(&st67_config);
    if (status == ST67W611_OK) {
        status = st67w611_start();
    }
    uint32_t dt = xTaskGetTickCount() - t0;
    LOGI(TAG, "W6X_Init returned: %d (took %lu ms)", status, dt);
    if (status != ST67W611_OK) {
        LOGE(TAG, "W6X_Init failed: %d", status);
        wifi_state = WIFI_STATE_ERROR;
        return NULL;
    }

    /* Step 3: Initialize WiFi subsystem */
    LOGI(TAG, "Calling W6X_WiFi_Init()...");
    t0 = xTaskGetTickCount();
    /* T01 owns the network stack inside ST67. The host must not create a
     * W6X_Netif/lwIP adapter; initialize the ST67 network component instead. */
#if (ST67_ARCH == 1)
    wifi_initialized = 1;
    wifi_state = WIFI_STATE_INIT;
    LOGI(TAG, "WiFi driver initialized successfully in T01 mode");
    return NULL;
#else
    /* Step 5: Get MAC address from ST67W611 module (after Wi-Fi init) */
    get_mac_address(wifi_mac_addr);
    LOGI(TAG, "  MAC: %02X:%02X:%02X:%02X:%02X:%02X",
         wifi_mac_addr[0], wifi_mac_addr[1], wifi_mac_addr[2],
         wifi_mac_addr[3], wifi_mac_addr[4], wifi_mac_addr[5]);

    /* Default: DHCP mode */
    ipaddr.addr = 0;
    netmask.addr = 0;
    gw.addr = 0;

    /* Add WiFi station interface to lwIP.
     * init callback is NULL (we configure netif fields manually below).
     * linkoutput is set after wifi_netif_init() by wifi_netif_set_netif(). */
    struct netif* netif_ptr = netif_add(&wifi_netif,
                                         &ipaddr,
                                         &netmask,
                                         &gw,
                                         NULL,           /* state */
                                         wifi_netif_init_cb,
                                         tcpip_input);

    if (netif_ptr == NULL) {
        LOGE(TAG, "Failed to add WiFi interface");
        wifi_state = WIFI_STATE_ERROR;
        return NULL;
    }

#if LWIP_NETIF_HOSTNAME
    netif_set_hostname(netif_ptr, "GatewayV2-WIFI");
#endif

    /* Set MAC address from ST67W611 module */
    wifi_netif.hwaddr_len = 6;
    memcpy(wifi_netif.hwaddr, wifi_mac_addr, 6);

    /* Set interface name */
    wifi_netif.name[0] = 'w';
    wifi_netif.name[1] = 'l';

    /* Set TX callback - lwIP calls this via netif->linkoutput */
    wifi_netif.linkoutput = wifi_netif_output;

    /* Set interface administratively up - link status is managed by wifi_event_handler */
    netif_set_up(&wifi_netif);

    /* Register netif with wifi_netif adapter before starting */
    wifi_netif_set_netif(&wifi_netif);

    /* Initialize ST67W6X netif adapter (W6X_Netif_Init + RX task) */
    if (wifi_netif_init() != 0) {
        LOGE(TAG, "Failed to initialize WiFi netif adapter");
        wifi_state = WIFI_STATE_ERROR;
        return NULL;
    }

    /* Register status callback for IP change notifications */
    wifi_netif_set_status_callback();

    wifi_initialized = 1;
    wifi_state = WIFI_STATE_INIT;

    LOGI(TAG, "WiFi driver initialized successfully");

    return &wifi_netif;
#endif
}

/**
 * @brief Start WiFi (bring interface up administratively)
 */
int wifi_driver_start(void) {
    if (!wifi_initialized) {
        LOGE(TAG, "WiFi not initialized");
        return -1;
    }

    LOGI(TAG, "Starting WiFi interface");

    /* Bring interface administratively up via thread-safe netifapi.
     * Link state is driven by netif_link_sta_up/down_cb callbacks. */
#if (ST67_ARCH == 2)
    (void)netifapi_netif_set_up(&wifi_netif);
#endif

    return 0;
}

/**
 * @brief Stop WiFi
 */
int wifi_driver_stop(void) {
    if (!wifi_initialized) {
        return -1;
    }

    LOGI(TAG, "Stopping WiFi");

    if (st67w611_disconnect() != ST67W611_OK) {
        LOGW(TAG, "ST67W611 disconnect returned an error");
    }

    wifi_connected = 0;
    wifi_state = WIFI_STATE_IDLE;
#if (ST67_ARCH == 2)
    /* Use thread-safe netifapi variants; direct netif_set_* calls are
     * not safe from outside the tcpip thread. */
    (void)netifapi_netif_set_link_down(&wifi_netif);
    (void)netifapi_netif_set_down(&wifi_netif);
#endif

    return 0;
}

/**
 * @brief Connect to WiFi network
 */
int wifi_driver_connect(const char *ssid, const char *pass) {
    if (!wifi_initialized) {
        LOGE(TAG, "WiFi not initialized");
        return -1;
    }

    if (ssid == NULL || strlen(ssid) == 0) {
        LOGE(TAG, "SSID cannot be empty");
        return -1;
    }

    /* W6X_WiFi_Connect rejects a new request while the station is already
     * connected. Explicitly tear down the old association before switching AP. */
    if (wifi_connected || wifi_state == WIFI_STATE_CONNECTED ||
        wifi_state == WIFI_STATE_GOT_IP) {
        LOGI(TAG, "Disconnecting current AP before connecting to %s", ssid);
        if (wifi_driver_disconnect() != 0) {
            LOGE(TAG, "Failed to disconnect current AP");
            return -1;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }

#if (ST67_ARCH == 2)
    /* The netif is already set administratively UP at init time and stays up
     * across reconnects. Only the link state changes (controlled by
     * netif_link_sta_up/down_cb via netifapi_*). Do NOT call netifapi_netif_set_up
     * here: doing so triggers wifi_netif_status_cb from inside the tcpip thread
     * while tcpip holds logger_mutex. If the W6X event task fires concurrently
     * and calls LOGI before netifapi_netif_set_link_up, it waits for
     * logger_mutex → then netifapi waits for tcpip → deadlock. */
    if (!netif_is_up(&wifi_netif)) {
        /* Only restore admin-up if someone explicitly brought it down. */
        (void)netifapi_netif_set_up(&wifi_netif);
    }
#endif

    st67w611_state_t st67_state = ST67W611_STATE_OFF;
    if (st67w611_get_state(&st67_state) != ST67W611_OK) {
        LOGE(TAG, "Unable to read ST67 state");
        wifi_state = WIFI_STATE_ERROR;
        return -1;
    }

    if (st67_state == ST67W611_STATE_ERROR) {
        if (wifi_driver_recover_st67() != 0) {
            return -1;
        }
        st67_state = ST67W611_STATE_READY;
    }
    if (st67_state != ST67W611_STATE_READY &&
        st67_state != ST67W611_STATE_CONNECTED) {
        LOGE(TAG, "ST67 is not ready for connect: state=%d", st67_state);
        wifi_state = WIFI_STATE_ERROR;
        return -1;
    }

    /* Store credentials */
    strncpy(wifi_ssid, ssid, sizeof(wifi_ssid) - 1);
    wifi_ssid[sizeof(wifi_ssid) - 1] = '\0';

    if (pass) {
        strncpy(wifi_password, pass, sizeof(wifi_password) - 1);
        wifi_password[sizeof(wifi_password) - 1] = '\0';
    } else {
        wifi_password[0] = '\0';
    }

    LOGI(TAG, "Connecting to WiFi: %s", ssid);

    /* Publish CONNECTING before entering the vendor call. The ST67 connect
     * operation may wait for several modem events; keeping the old state
     * until it returns leaves callers without an in-progress guard. */
    wifi_state = WIFI_STATE_CONNECTING;
    wifi_connected = 0U;

    /* Connect to AP */
    st67w611_status_t status = st67w611_connect(ssid, pass);
    if (status != ST67W611_OK) {
        LOGE(TAG, "ST67W611 connect failed: %d", status);
        wifi_connected = 0U;
        s_consecutive_connect_failures++;
        if (s_consecutive_connect_failures <= WIFI_SOFT_CONNECT_FAILURE_LIMIT) {
            st67w611_status_t disconnect_status = st67w611_disconnect();
            if (disconnect_status == ST67W611_OK) {
                wifi_state = WIFI_STATE_DISCONNECTED;
                LOGW(TAG, "WiFi soft recovery after connect failure %u/%u; hard reset deferred",
                     (unsigned)s_consecutive_connect_failures,
                     (unsigned)WIFI_SOFT_CONNECT_FAILURE_LIMIT);
                return -1;
            }
            LOGW(TAG, "WiFi soft recovery failed: %d", disconnect_status);
        }
        wifi_state = WIFI_STATE_ERROR;
        LOGW(TAG, "WiFi hard recovery required after %u consecutive failures",
             (unsigned)s_consecutive_connect_failures);
        return -1;
    }

    return 0;
}

int wifi_driver_scan(void)
{
    is_scanning = true;
    snprintf(wifi_scan_json, sizeof(wifi_scan_json), "[");
    int ret = (st67w611_scan(wifi_scan_result_handler, NULL) == ST67W611_OK) ? 0 : -1;
    if (ret != 0) {
        is_scanning = false;
        snprintf(wifi_scan_json, sizeof(wifi_scan_json), "[]");
    }
    return ret;
}

int wifi_driver_recover(void)
{
    return wifi_driver_recover_st67();
}

int wifi_driver_get_st67_state(int *out_state)
{
    st67w611_state_t state;
    if (out_state == NULL || st67w611_get_state(&state) != ST67W611_OK) {
        return -1;
    }
    *out_state = (int)state;
    return 0;
}

int wifi_driver_get_ip(uint8_t ip[4], uint8_t gateway[4], uint8_t netmask[4])
{
    return st67w611_get_ip(ip, gateway, netmask) == ST67W611_OK ? 0 : -1;
}

int wifi_driver_ping(const char *location)
{
    return st67w611_ping(location, 5000U) == ST67W611_OK ? 0 : -1;
}

/**
 * @brief Disconnect from WiFi
 */
int wifi_driver_disconnect(void) {
    if (!wifi_initialized) {
        return -1;
    }

    LOGI(TAG, "Disconnecting WiFi");

    if (st67w611_disconnect() != ST67W611_OK) {
        return -1;
    }

    wifi_connected = 0;
    wifi_state = WIFI_STATE_DISCONNECTED;
#if (ST67_ARCH == 2)
    /* Use thread-safe netifapi variants; direct netif_set_* calls are
     * not safe from outside the tcpip thread and can deadlock. */
    (void)netifapi_netif_set_link_down(&wifi_netif);
    (void)netifapi_netif_set_down(&wifi_netif);
#endif

    return 0;
}

/**
 * @brief Send data (handled by lwIP via netif)
 */
int wifi_driver_send(const uint8_t *data, uint16_t len) {
    (void)data;
    (void)len;
    /* Not used directly - lwIP handles send via netif */
    return -1;
}

/**
 * @brief Receive data (handled by lwIP via netif)
 */
int wifi_driver_recv(uint8_t *data, uint16_t max_len) {
    (void)data;
    (void)max_len;
    /* Not used directly - lwIP handles recv via netif */
    return -1;
}

/**
 * @brief Get connection status
 */
uint8_t wifi_driver_status(void) {
    return wifi_connected;
}

/**
 * @brief Register event callback
 */
void wifi_driver_set_event_cb(wifi_event_cb_t cb) {
    g_wifi_cb = cb;
}

void wifi_driver_notify_netif_got_ip(void) {
    /* The netif status callback is authoritative for host-side DHCP.  Some
     * ST67 firmware versions do not emit EVENT_GOT_IP, so bridge that event
     * here without requiring the application to infer it from polling. */
    wifi_state = WIFI_STATE_GOT_IP;
    if (g_wifi_cb != NULL) {
        g_wifi_cb((uint8_t)WIFI_STATE_GOT_IP);
    }
}

/**
 * @brief Get signal strength (RSSI)
 * @return RSSI value (negative dBm) or 0 if not connected
 */
int8_t wifi_driver_get_rssi(void) {
    if (!wifi_connected) {
        return 0;
    }

    /* Query from ST67W611 module */
    int8_t rssi = 0;
    /* W6X_WiFi_Station_GetRSSI(&rssi); */
    return rssi;
}

/**
 * @brief Get WiFi state
 * @return Current WiFi state
 */
wifi_state_t wifi_driver_get_state(void) {
    return wifi_state;
}

bool wifi_driver_is_connect_in_progress(void)
{
    return wifi_state == WIFI_STATE_CONNECTING;
}

struct netif *wifi_driver_get_netif(void) {
#if (ST67_ARCH == 2)
    return wifi_initialized ? &wifi_netif : NULL;
#else
    return NULL;
#endif
}

/**
 * @brief WiFi task - periodic status monitoring
 * State is driven by wifi_event_handler callbacks; this task logs periodic
 * connection status for diagnostics.
 */
static void wifi_task(void *params) {
    (void)params;

    LOGI(TAG, "WiFi task started");

    while (1) {
        if (wifi_initialized) {
            const char *state_str = "IDLE";
            switch (wifi_state) {
                case WIFI_STATE_INIT:        state_str = "INIT";       break;
                case WIFI_STATE_CONNECTING:   state_str = "CONNECTING"; break;
                case WIFI_STATE_CONNECTED:    state_str = "CONNECTED";  break;
                case WIFI_STATE_GOT_IP:      state_str = "GOT_IP";    break;
                case WIFI_STATE_DISCONNECTED: state_str = "DISCONNECTED"; break;
                default: break;
            }
            LOGD(TAG, "WiFi status: %s", state_str);
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

/**
 * @brief Create WiFi monitoring task
 */
int wifi_driver_start_task(void) {
    BaseType_t ret;

    if (!wifi_initialized) {
        return -1;
    }

    /* This API may be called by more than one startup path.  Protect the
     * check/create pair so only one monitoring task can ever be created. */
    taskENTER_CRITICAL();
    if (wifi_task_handle != NULL) {
        taskEXIT_CRITICAL();
        return 0;
    }

    ret = xTaskCreate(wifi_task,
                      "WiFiTask",
                      WIFI_TASK_STACK_SIZE,
                      NULL,
                      WIFI_TASK_PRIO,
                      &wifi_task_handle);
    if (ret != pdPASS) {
        wifi_task_handle = NULL;
    }
    taskEXIT_CRITICAL();

    return (ret == pdPASS) ? 0 : -1;
}

/**
 * @brief ST reference-style initialization task.
 *
 * The official NUCLEO-H563ZI T02 project creates a dedicated ST67 task before
 * the scheduler and runs main_app() from that task.  GatewayV2 keeps its
 * network-manager integration, but follows the same ownership boundary:
 * W6X_Init() and W6X_WiFi_Init() never run on the shell or network task.
 */
static void wifi_startup_task(void *arg) {
    (void)arg;

    LOGI(TAG, "ST67W6X startup task started");
    LOGI(TAG, "ST67W6X task stack high water mark: %lu words",
         (uint32_t)uxTaskGetStackHighWaterMark(NULL));

#if (ST67_ARCH == 2)
    struct netif *nif = wifi_driver_init();
    if (nif == NULL) {
        LOGE(TAG, "ST67W6X startup failed");
        wifi_startup_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }
#else
    (void)wifi_driver_init();
    if (wifi_driver_get_state() == WIFI_STATE_ERROR) {
        LOGE(TAG, "ST67W6X startup failed");
        wifi_startup_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }
#endif

    if (wifi_driver_start_task() != 0) {
        LOGE(TAG, "Failed to start WiFi monitoring task");
    }

    LOGI(TAG, "ST67W6X startup task completed");
    wifi_startup_task_handle = NULL;
    vTaskDelete(NULL);
}

int wifi_driver_startup_task_create(void) {
    if (wifi_initialized || wifi_startup_task_handle != NULL) {
        return 0;
    }

    BaseType_t ret = xTaskCreate(wifi_startup_task,
                                 "ST67W6XTask",
                                 4096U >> 2U,
                                 NULL,
                                 8U,
                                 &wifi_startup_task_handle);
    return (ret == pdPASS) ? 0 : -1;
}
