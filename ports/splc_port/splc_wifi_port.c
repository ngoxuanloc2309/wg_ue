/**
 * @file splc_wifi_port.c
 * @brief See splc_wifi_port.h.
 */
#include "splc_wifi_port.h"

#if SPLC_WIFI_ENABLE

#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"
#include "lwip/netif.h"
#include "lwip/def.h"

#include "plc_wifi.h"
#include "network_manager.h"
#include "wifi_netif.h"
#include "wifi_driver.h"
#include "st67w611.h"
#include "config_manager.h"      /* config_save() */
#include "splc_log.h"

#define TAG "SPLC_WIFI"

/* Commands for the worker. Set from the engine task, cleared by the worker. */
#define CMD_CONNECT     0x01U
#define CMD_DISCONNECT  0x02U
#define CMD_SCAN        0x04U
#define CMD_SAVE        0x08U
#define CMD_ERASE       0x10U

static TaskHandle_t         s_task;
static volatile uint32_t    s_cmd;
static splc_wifi_config_t   s_cfg;            /* what CONNECT / SAVE use (copy made by connect()/save()) */
static volatile bool        s_connecting;     /* report CONNECTING until the network task has taken over */

/* Scan results: written by the worker / the ST67 callback, read by the engine task. */
static splc_wifi_ap_t       s_ap[PLC_WIFI_MAX_APS];
static volatile uint8_t     s_ap_count;
static volatile uint16_t    s_scan_state = PLC_WIFI_SCAN_IDLE;

static void post(uint32_t bits)
{
    taskENTER_CRITICAL();
    s_cmd |= bits;
    taskEXIT_CRITICAL();
    if (s_task != NULL) {
        xTaskNotifyGive(s_task);
    }
}

static uint32_t take_cmds(void)
{
    taskENTER_CRITICAL();
    const uint32_t c = s_cmd;
    s_cmd = 0U;
    taskEXIT_CRITICAL();
    return c;
}

/* ---- callbacks of plc_wifi (engine task, must return at once) -------------------- */

static int op_connect(void *ctx, const splc_wifi_config_t *cfg)
{
    (void)ctx;
    if ((cfg->control_flags & PLC_WIFI_CTRL_ENABLE) == 0U) {
        return PLC_WIFI_ERR_HW_NOT_READY;        /* Enable = 0: the App asks for Wi-Fi off */
    }
    taskENTER_CRITICAL();
    s_cfg = *cfg;
    s_connecting = true;
    taskEXIT_CRITICAL();
    post(CMD_CONNECT);
    return PLC_WIFI_ERR_NONE;
}

static int op_disconnect(void *ctx)
{
    (void)ctx;
    s_connecting = false;
    post(CMD_DISCONNECT);
    return PLC_WIFI_ERR_NONE;
}

static bool op_status(void *ctx, splc_wifi_status_t *out)
{
    (void)ctx;
    memset(out, 0, sizeof(*out));
    switch (wifi_driver_get_state()) {
    case WIFI_STATE_CONNECTING:  out->conn_status = PLC_WIFI_CONN_CONNECTING; break;
    case WIFI_STATE_CONNECTED:   out->conn_status = PLC_WIFI_CONN_CONNECTED;  break;
    case WIFI_STATE_GOT_IP:      out->conn_status = PLC_WIFI_CONN_GOT_IP;     break;
    case WIFI_STATE_ERROR:       out->error_code  = PLC_WIFI_ERR_DRIVER;      /* fall through */
    default:                     out->conn_status = PLC_WIFI_CONN_DISCONNECTED; break;
    }
    if (s_connecting) {
        out->conn_status = PLC_WIFI_CONN_CONNECTING;   /* the old link may still be up for a moment */
        out->error_code = 0U;
    }
    struct netif *nif = network_interface_get_netif(NET_IF_WIFI);
    if (nif != NULL) {
        out->ip = lwip_ntohl(ip4_addr_get_u32(netif_ip4_addr(nif)));
        memcpy(out->mac, nif->hwaddr, 6U);
    }
    out->rssi_dbm = 0;                               /* see the header: the driver has none */
    out->scan_state = s_scan_state;
    out->ap_count   = s_ap_count;
    return true;
}

static int op_scan_start(void *ctx)
{
    (void)ctx;
    if (s_scan_state == PLC_WIFI_SCAN_SCANNING) {
        return PLC_WIFI_ERR_BUSY;
    }
    s_ap_count = 0U;
    s_scan_state = PLC_WIFI_SCAN_SCANNING;
    post(CMD_SCAN);
    return PLC_WIFI_ERR_NONE;
}

static bool op_scan_ap(void *ctx, uint8_t index, splc_wifi_ap_t *out)
{
    (void)ctx;
    if (index >= s_ap_count) {
        return false;
    }
    taskENTER_CRITICAL();
    *out = s_ap[index];
    taskEXIT_CRITICAL();
    return true;
}

/* load: the host's config.json was loaded at boot (config_load()). */
static bool op_load(void *ctx, splc_wifi_config_t *out)
{
    (void)ctx;
    network_interface_config_t c;
    if (network_interface_get_config(NET_IF_WIFI, &c) != 0 || c.ssid[0] == '\0') {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->control_flags = (uint16_t)((c.enabled ? PLC_WIFI_CTRL_ENABLE : 0U) |
                                    (c.dhcp ? PLC_WIFI_CTRL_DHCP : 0U) |
                                    PLC_WIFI_CTRL_AUTO_RECONNECT);
    strncpy(out->ssid, c.ssid, sizeof(out->ssid) - 1U);
    strncpy(out->password, c.password, sizeof(out->password) - 1U);
    out->static_ip      = lwip_ntohl(c.static_ip.ip);
    out->static_subnet  = lwip_ntohl(c.static_ip.netmask);
    out->static_gateway = lwip_ntohl(c.static_ip.gateway);
    out->static_dns     = lwip_ntohl(c.static_ip.dns_primary);
    return true;
}

static bool op_save(void *ctx, const splc_wifi_config_t *cfg)
{
    (void)ctx;
    taskENTER_CRITICAL();
    s_cfg = *cfg;
    taskEXIT_CRITICAL();
    post(CMD_SAVE);
    return true;                                     /* queued; the worker logs a failed write */
}

static void op_erase(void *ctx)
{
    (void)ctx;
    post(CMD_ERASE);
}

/* ---- worker -------------------------------------------------------------------- */

static void ap_collect_cb(st67w611_status_t status, const st67w611_scan_result_t *r, void *user)
{
    (void)user;
    if (status != ST67W611_OK) {
        s_scan_state = PLC_WIFI_SCAN_FAILED;
        return;
    }
    if (r == NULL) {                                  /* end of the scan */
        s_scan_state = PLC_WIFI_SCAN_COMPLETED;
        return;
    }
    if (s_ap_count < PLC_WIFI_MAX_APS) {
        splc_wifi_ap_t a;
        memset(&a, 0, sizeof(a));
        strncpy(a.ssid, r->ssid, sizeof(a.ssid) - 1U);
        a.rssi_dbm  = r->rssi;
        a.auth_mode = 0U;                             /* not reported by the driver */
        a.channel   = r->channel;
        memcpy(a.bssid, r->mac, 6U);
        taskENTER_CRITICAL();
        s_ap[s_ap_count] = a;
        s_ap_count = (uint8_t)(s_ap_count + 1U);
        taskEXIT_CRITICAL();
    }
}

static void to_net_config(const splc_wifi_config_t *c, network_interface_config_t *n)
{
    n->enabled = true;
    n->dhcp    = (c->control_flags & PLC_WIFI_CTRL_DHCP) != 0U;
    strncpy(n->ssid, c->ssid, sizeof(n->ssid) - 1U);
    n->ssid[sizeof(n->ssid) - 1U] = '\0';
    strncpy(n->password, c->password, sizeof(n->password) - 1U);
    n->password[sizeof(n->password) - 1U] = '\0';
    n->static_ip.ip          = lwip_htonl(c->static_ip);
    n->static_ip.netmask     = lwip_htonl(c->static_subnet);
    n->static_ip.gateway     = lwip_htonl(c->static_gateway);
    n->static_ip.dns_primary = lwip_htonl(c->static_dns);
}

static void do_connect(const splc_wifi_config_t *c)
{
    network_interface_config_t n;
    if (network_interface_get_config(NET_IF_WIFI, &n) != 0) {
        s_connecting = false;
        return;
    }
    to_net_config(c, &n);
    (void)network_interface_set_config(NET_IF_WIFI, &n);
    if (!n.dhcp) {
        network_config_t all;
        if (network_manager_get_config(&all) == 0) {
            (void)wifi_netif_apply_config(&all);       /* static address on the netif */
        }
    }
    network_manager_request_wifi_connect();            /* the network task does the slow part */
    /* Keep reporting CONNECTING until the network task has actually started the new
     * connection (it may first have to drop the old one), so the library cannot take a
     * leftover GOT_IP of the previous AP for success and save the new password. */
    for (unsigned i = 0U; i < 200U && wifi_driver_get_state() != WIFI_STATE_CONNECTING; i++) {
        vTaskDelay(pdMS_TO_TICKS(50U));
    }
    s_connecting = false;
}

static void do_disconnect(void)
{
    network_interface_config_t n;
    if (network_interface_get_config(NET_IF_WIFI, &n) == 0) {
        n.enabled = false;                             /* the network task must not reconnect */
        (void)network_interface_set_config(NET_IF_WIFI, &n);
    }
    if (wifi_driver_disconnect() != 0) {
        log_warn(TAG, "disconnect request failed in the driver");
    }
}

static void do_save(const splc_wifi_config_t *c)
{
    network_interface_config_t n;
    if (network_interface_get_config(NET_IF_WIFI, &n) == 0) {
        to_net_config(c, &n);
        (void)network_interface_set_config(NET_IF_WIFI, &n);
    }
    if (config_save() != 0) {
        log_warn(TAG, "saving the Wi-Fi credentials failed (config_save)");
    } else {
        log_info(TAG, "Wi-Fi credentials saved to the host configuration");
    }
}

static void do_erase(void)
{
    network_interface_config_t n;
    if (network_interface_get_config(NET_IF_WIFI, &n) == 0) {
        n.enabled = false;
        memset(n.ssid, 0, sizeof(n.ssid));
        memset(n.password, 0, sizeof(n.password));
        (void)network_interface_set_config(NET_IF_WIFI, &n);
    }
    if (config_save() != 0) {
        log_warn(TAG, "forgetting the Wi-Fi credentials: config_save failed");
    }
}

static void wifi_worker(void *arg)
{
    (void)arg;
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        const uint32_t cmds = take_cmds();
        splc_wifi_config_t c;
        taskENTER_CRITICAL();
        c = s_cfg;
        taskEXIT_CRITICAL();

        if ((cmds & CMD_DISCONNECT) != 0U) { do_disconnect(); }
        if ((cmds & CMD_CONNECT) != 0U)    { do_connect(&c); }
        if ((cmds & CMD_SAVE) != 0U)       { do_save(&c); }
        if ((cmds & CMD_ERASE) != 0U)      { do_erase(); }
        if ((cmds & CMD_SCAN) != 0U) {
            if (st67w611_scan(ap_collect_cb, NULL) != ST67W611_OK) {
                s_scan_state = PLC_WIFI_SCAN_FAILED;
            }
        }
    }
}

void splc_wifi_port_init(void)
{
    if (s_task == NULL &&
        xTaskCreate(wifi_worker, "SPLC_WIFI", (configSTACK_DEPTH_TYPE)SPLC_HOST_WIFI_TASK_STACK_WORDS,
                    NULL, SPLC_HOST_WIFI_TASK_PRIO, &s_task) != pdPASS) {
        log_error(TAG, "worker task creation failed: Wi-Fi block not available");
        return;
    }
    const splc_wifi_ops_t ops = {
        .ctx        = NULL,
        .connect    = op_connect,
        .disconnect = op_disconnect,
        .status     = op_status,
        .scan_start = op_scan_start,
        .scan_ap    = op_scan_ap,
        .load       = op_load,
        .save       = op_save,
        .erase      = op_erase,
    };
    if (!splc_wifi_register(&ops)) {
        log_error(TAG, "registration with SimplePLC failed");
    }
}

#endif /* SPLC_WIFI_ENABLE */