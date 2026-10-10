/**
 * @file lte_driver.c
 * @brief LTE PPPoS (A7680C) Driver
 */
#include "lte_driver.h"
#include "logger.h"
#include "a7680c.h"
#include "cmsis_os2.h"
#include "lwip/netif.h"
#include "lwip/tcpip.h"
#include "lwip/ip_addr.h"
#include "lwip/dns.h"
#include "netif/ppp/pppapi.h"
#include "netif/ppp/pppos.h"
#include "netif/ppp/ppp.h"
#include "app_config.h"
#include <string.h>

static const char *TAG = "lte_driver";

static lte_event_cb_t g_lte_cb = NULL;
static uint8_t lte_connected = 0;
static uint8_t lte_initialized = 0;

/* PPP Control Block */
static ppp_pcb *ppp_context = NULL;
static struct netif ppp_netif;

/* LTE connection state */
typedef enum {
    LTE_STATE_IDLE = 0,
    LTE_STATE_INIT,
    LTE_STATE_POWER_ON,
    LTE_STATE_AT_READY,
    LTE_STATE_NETWORK_SEARCH,
    LTE_STATE_REGISTERED,
    LTE_STATE_CONNECTING,
    LTE_STATE_PPP_MODE,
    LTE_STATE_CONNECTED,
    LTE_STATE_DISCONNECTED,
    LTE_STATE_HARD_RESET
} lte_state_t;

static lte_state_t lte_state = LTE_STATE_IDLE;
static char apn_string[64] = "internet";
static char ppp_username[33] = "";
static char ppp_password[65] = "";
static uint8_t ppp_credentials_dirty = 0U;
static lte_diag_t lte_diag;
static uint32_t ppp_backoff_ms = LTE_PPP_BACKOFF_INITIAL_MS;
static uint32_t hard_reset_window_start;
static uint32_t retry_not_before;
static uint32_t lte_dns_primary;
static uint32_t lte_dns_secondary;

static uint32_t lte_now_ms(void) {
    return (uint32_t)((uint64_t)osKernelGetTickCount() * 1000U /
                      osKernelGetTickFreq());
}

/* FreeRTOS Task */
static osThreadId_t lteTaskHandle;
const osThreadAttr_t lteTask_attributes = {
  .name = "lteTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

static void lte_driver_task(void *argument);
static u32_t ppp_output_cb(ppp_pcb *pcb, const void *data, u32_t len, void *ctx);
static void ppp_status_cb(ppp_pcb *pcb, int err_code, void *ctx);

struct netif* lte_driver_init(void) {
    if (lte_initialized) return &ppp_netif;
    
    g_lte_cb = NULL;
    lte_connected = 0;
    lte_state = LTE_STATE_IDLE;
    memset(&lte_diag, 0, sizeof(lte_diag));
    lte_diag.status = LTE_STATUS_IDLE;
    lte_dns_primary = 0U;
    lte_dns_secondary = 0U;
    
    /* Create PPP PCB */
    ppp_context = pppapi_pppos_create(&ppp_netif, ppp_output_cb, ppp_status_cb, NULL);
    if (ppp_context == NULL) {
        LOGE(TAG, "Failed to create PPP context");
        return NULL;
    }
    
    /* The network manager exclusively owns lwIP's default route.  Selecting
     * PPP here would route traffic to LTE before PPP has an address. */
    ppp_set_auth(ppp_context, PPPAUTHTYPE_ANY, ppp_username, ppp_password);
#if LWIP_DNS
    ppp_set_usepeerdns(ppp_context, 1);
#endif
    
    a7680c_init(); /* Initialize modem HAL and DMA */
    
    lteTaskHandle = osThreadNew(lte_driver_task, NULL, &lteTask_attributes);
    
    lte_initialized = 1;
    LOGI(TAG, "LTE PPPoS initialized");
    return &ppp_netif;
}
int lte_driver_start(void) {
    if (!lte_initialized) return -1;
    /* network_task polls this API.  Do not restart an in-progress modem
     * state machine on every poll. */
    if (lte_state == LTE_STATE_IDLE || lte_state == LTE_STATE_DISCONNECTED) {
        lte_state = LTE_STATE_POWER_ON;
        lte_diag.status = LTE_STATUS_RECOVERING;
    }
    return 0;
}

int lte_driver_stop(void) {
    if (!lte_initialized) return -1;
    if (lte_state == LTE_STATE_PPP_MODE || lte_state == LTE_STATE_CONNECTED) {
        pppapi_close(ppp_context, 0);
    }
    lte_state = LTE_STATE_IDLE;
    lte_connected = 0;
    lte_diag.status = LTE_STATUS_IDLE;
    return 0;
}

void lte_driver_init_callback(lte_event_cb_t cb) { g_lte_cb = cb; }
int lte_driver_connect(void) { return lte_driver_start(); }
int lte_driver_disconnect(void) { return lte_driver_stop(); }
int lte_driver_send(const uint8_t *data, uint16_t len) { return -1; }
int lte_driver_recv(uint8_t *data, uint16_t max_len) { return -1; }
uint8_t lte_driver_status(void) { return lte_connected; }
void lte_driver_set_apn(const char *apn) {
    if (apn) {
        strncpy(apn_string, apn, sizeof(apn_string) - 1);
        apn_string[sizeof(apn_string) - 1] = '\0';
    }
}
int lte_driver_get_csq(void) { return lte_diag.csq; }
const char* lte_driver_get_operator(void) { return lte_diag.operator_id; }

void lte_driver_set_credentials(const char *apn, const char *username,
                                const char *password) {
    char old_username[sizeof(ppp_username)];
    char old_password[sizeof(ppp_password)];

    strncpy(old_username, ppp_username, sizeof(old_username));
    old_username[sizeof(old_username) - 1U] = '\0';
    strncpy(old_password, ppp_password, sizeof(old_password));
    old_password[sizeof(old_password) - 1U] = '\0';

    lte_driver_set_apn(apn);
    if (username != NULL) {
        strncpy(ppp_username, username, sizeof(ppp_username) - 1U);
        ppp_username[sizeof(ppp_username) - 1U] = '\0';
    }
    if (password != NULL) {
        strncpy(ppp_password, password, sizeof(ppp_password) - 1U);
        ppp_password[sizeof(ppp_password) - 1U] = '\0';
    }

    if (strcmp(old_username, ppp_username) != 0 ||
        strcmp(old_password, ppp_password) != 0) {
        /* ppp_set_auth() is only legal while the PCB is PPP_PHASE_DEAD.
         * The network manager calls this setter periodically, including
         * while PPP is negotiating, so defer the actual lwIP update. */
        ppp_credentials_dirty = 1U;
    }
}

lte_status_t lte_driver_get_status(void) { return lte_diag.status; }

void lte_driver_get_diag(lte_diag_t *diag) {
    if (diag != NULL) {
        *diag = lte_diag;
    }
}

void lte_driver_get_dns(uint32_t *primary, uint32_t *secondary)
{
    if (primary != NULL) *primary = lte_dns_primary;
    if (secondary != NULL) *secondary = lte_dns_secondary;
}

static u32_t ppp_output_cb(ppp_pcb *pcb, const void *data, u32_t len, void *ctx) {
    int sent = a7680c_transmit((const uint8_t*)data, len);
    return (sent > 0) ? sent : 0;
}

static void ppp_status_cb(ppp_pcb *pcb, int err_code, void *ctx) {
    struct netif *pppif = ppp_netif(pcb);
    
    switch(err_code) {
        case PPPERR_NONE:
            LOGI(TAG, "PPP Connected! IP: %s", ip4addr_ntoa(netif_ip4_addr(pppif)));
#if LWIP_DNS
            lte_dns_primary = dns_getserver(0)->addr;
            lte_dns_secondary = dns_getserver(1)->addr;
            LOGI(TAG, "PPP DNS: primary=%s secondary=%s",
                 ip4addr_ntoa(dns_getserver(0)),
                 ip4addr_ntoa(dns_getserver(1)));
#endif
            lte_connected = 1;
            lte_state = LTE_STATE_CONNECTED;
            if (g_lte_cb) g_lte_cb(1);
            break;
        default:
            LOGE(TAG, "PPP Error/Disconnected: %d", err_code);
            lte_connected = 0;
            lte_diag.status = LTE_STATUS_DEGRADED;
            lte_diag.last_error = err_code;
            lte_diag.ppp_retries++;
            retry_not_before = lte_now_ms() + ppp_backoff_ms;
            ppp_backoff_ms = (ppp_backoff_ms < LTE_PPP_BACKOFF_MAX_MS / 2U) ?
                             ppp_backoff_ms * 2U : LTE_PPP_BACKOFF_MAX_MS;
            if (lte_state == LTE_STATE_CONNECTED || lte_state == LTE_STATE_PPP_MODE) {
                lte_state = LTE_STATE_INIT; /* Auto reconnect */
            }
            if (g_lte_cb) g_lte_cb(0);
            break;
    }
}

static int lte_apply_ppp_credentials(void) {
    if (ppp_context == NULL || ppp_context->phase != PPP_PHASE_DEAD) {
        return -1;
    }

    if (ppp_credentials_dirty != 0U) {
        ppp_set_auth(ppp_context, PPPAUTHTYPE_ANY, ppp_username, ppp_password);
        ppp_credentials_dirty = 0U;
    }
    return 0;
}

static void lte_driver_task(void *argument) {
    uint32_t fail_counter = 0;

    while (1) {
        if (retry_not_before != 0U &&
            (int32_t)(lte_now_ms() - retry_not_before) < 0) {
            osDelay(100);
            continue;
        }

        switch (lte_state) {
            case LTE_STATE_IDLE:
                osDelay(100);
                break;
                
            case LTE_STATE_POWER_ON:
                a7680c_power_on();
                lte_state = LTE_STATE_INIT;
                fail_counter = 0;
                break;
                
            case LTE_STATE_INIT:
                if (a7680c_wait_at_ready() == 0) {
                    lte_state = LTE_STATE_AT_READY;
                    fail_counter = 0;
                } else {
                    fail_counter++;
                    if (fail_counter > 10) lte_state = LTE_STATE_HARD_RESET;
                    osDelay(2000);
                }
                break;
                
            case LTE_STATE_AT_READY:
            case LTE_STATE_NETWORK_SEARCH:
                {
                    int registration_status = a7680c_wait_network_registration();
                    if (registration_status == 0) {
                    lte_state = LTE_STATE_REGISTERED;
                    fail_counter = 0;
                    } else if (registration_status == 1 || registration_status == 2) {
                        /* The modem is alive; do not turn normal network
                         * search time into a modem hard-reset loop. */
                        fail_counter = 0;
                        LOGI(TAG, "Waiting for network...");
                        osDelay(2000);
                    } else {
                        fail_counter++;
                        if (fail_counter > 10) lte_state = LTE_STATE_HARD_RESET;
                        osDelay(2000);
                    }
                }
                break;
                
            case LTE_STATE_REGISTERED:
                if (a7680c_setup_network(apn_string) == 0) {
                    lte_state = LTE_STATE_CONNECTING;
                } else {
                    lte_state = LTE_STATE_HARD_RESET;
                    osDelay(2000);
                }
                break;
                
            case LTE_STATE_CONNECTING:
                if (lte_apply_ppp_credentials() == 0 && a7680c_dial_ppp() == 0) {
                    lte_state = LTE_STATE_PPP_MODE;
                    pppapi_connect(ppp_context, 0);
                } else {
                    LOGW(TAG, "PPP PCB is still active or dial failed; delaying reconnect");
                    lte_state = LTE_STATE_HARD_RESET;
                }
                break;
                
            case LTE_STATE_PPP_MODE:
            case LTE_STATE_CONNECTED:
                {
                    uint8_t tmp[128];
                    int n = a7680c_receive(tmp, sizeof(tmp));
                    if (n > 0) {
                        pppos_input(ppp_context, tmp, n);
                    } else {
                        osDelay(5);
                    }
                }
                break;

            case LTE_STATE_HARD_RESET:
                {
                    uint32_t now = lte_now_ms();
                    if (hard_reset_window_start == 0U ||
                        (now - hard_reset_window_start) > LTE_HARD_RESET_WINDOW_MS) {
                        hard_reset_window_start = now;
                        lte_diag.hard_resets = 0U;
                    }
                    if (lte_diag.hard_resets >= LTE_HARD_RESET_LIMIT) {
                        LOGW(TAG, "LTE recovery cooldown after reset budget");
                        lte_diag.status = LTE_STATUS_ERROR;
                        retry_not_before = now + LTE_RECOVERY_COOLDOWN_MS;
                        lte_diag.hard_resets = 0U;
                        hard_reset_window_start = now;
                        lte_state = LTE_STATE_POWER_ON;
                    } else {
                        a7680c_hard_reset();
                        lte_diag.hard_resets++;
                        lte_diag.status = LTE_STATUS_RECOVERING;
                        lte_state = LTE_STATE_POWER_ON;
                    }
                }
                break;
                
            case LTE_STATE_DISCONNECTED:
                osDelay(100);
                break;
        }
    }
}

