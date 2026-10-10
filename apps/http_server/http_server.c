#include "gateway.h"
#include "ntp_client.h"
#include "telemetry_store.h"
/**
 * @file http_server.c
 * @brief Mongoose HTTP Server with Web Dashboard
 */
#include "http_server.h"
#include "logger.h"
#include "mongoose.h"
#include "mongoose_glue.h"
#include "FreeRTOS.h"
#include "task.h"
#include "lwip/netif.h"
#include "network_manager.h"
#include "config_manager.h"
#include "config_apply_manager.h"
#include "mongoose_lfs.h"
#include "ota.h"
#include "data_broker.h"
#include "eth_driver.h"
#include "usb_netif.h"
#include "rs485_driver.h"
#include "mqtt_client/mqtt_client.h"
#include "app_config.h"
#include "lwip/sockets.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "HTTPServer";
#define HTTP_CONFIG_MAX_BODY (16U * 1024U)
#define HTTP_MAX_ACTIVE_CONNECTIONS 4U

/* Mongoose Manager */
static struct mg_mgr s_mgr;
static StaticTask_t http_task_tcb;
static StackType_t http_task_stack[4096];
static char s_hostname[40] = "synaptix-gateway";
static struct mg_connection *s_mdns = NULL;
static uint32_t s_mdns_ip = 0U;
static bool s_mdns_ready_logged = false;
static bool s_eth_ready_logged = false;
static bool s_wifi_ready_logged = false;
static bool s_usb_ready_logged = false;

typedef struct {
    uint8_t kind;
    bool active;
    size_t expected;
    size_t received;
    bool has_sha256;
    uint8_t sha256[32];
    unsigned long owner_connection_id;
} http_ota_state_t;

typedef struct {
    bool connection_counted;
} http_connection_state_t;

_Static_assert(sizeof(http_connection_state_t) <= MG_DATA_SIZE,
               "HTTP connection state exceeds Mongoose c->data");

static volatile uint8_t s_ota_state = 0U;
static volatile size_t s_ota_expected = 0U;
static volatile size_t s_ota_received = 0U;
static http_ota_state_t s_ota_upload_state;
static http_ota_state_t s_ota_url_state;
static uint8_t s_http_active_connections = 0U;

static bool http_ota_content_length(struct mg_http_message *hm, size_t *length);
static bool http_ota_filename_valid(struct mg_http_message *hm);
static void http_ota_abort_state(http_ota_state_t *state);
static void ota_commit_task(void *arg);

static const char *http_ota_state_name(ota_state_t state)
{
    switch (state) {
        case OTA_STATE_DOWNLOADING: return "downloading";
        case OTA_STATE_VERIFYING: return "verifying";
        case OTA_STATE_PENDING_BOOT: return "pending_boot";
        case OTA_STATE_BOOTING: return "booting";
        case OTA_STATE_CONFIRMED: return "confirmed";
        case OTA_STATE_ERROR: return "error";
        default: return "idle";
    }
}

static bool http_ota_sha256(struct mg_http_message *hm, uint8_t digest[32])
{
    struct mg_str *header = mg_http_get_header(hm, "X-Firmware-SHA256");
    size_t i;
    if (header == NULL || header->len != 64U) return false;
    for (i = 0U; i < 32U; ++i) {
        char pair[3] = { header->buf[i * 2U], header->buf[i * 2U + 1U], '\0' };
        char *end = NULL;
        unsigned long value = strtoul(pair, &end, 16);
        if (end == pair || *end != '\0' || value > 255UL) return false;
        digest[i] = (uint8_t)value;
    }
    return true;
}

static void http_ota_url_cb(struct mg_connection *c, int ev, void *ev_data)
{
    if (ev == MG_EV_HTTP_HDRS) {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;
        size_t expected = 0U;
        uint8_t digest[32] = { 0 };
        bool has_digest = http_ota_sha256(hm, digest);
        if (mg_http_status(hm) < 200 || mg_http_status(hm) >= 300 ||
            !http_ota_content_length(hm, &expected) ||
            ota_begin((uint32_t)expected, 0U) != 0) {
            s_ota_state = 4U;
            c->is_closing = 1;
            return;
        }
        memset(&s_ota_url_state, 0, sizeof(s_ota_url_state));
        s_ota_url_state.kind = 2U;
        s_ota_url_state.active = true;
        s_ota_url_state.expected = expected;
        s_ota_url_state.has_sha256 = has_digest;
        if (has_digest) memcpy(s_ota_url_state.sha256, digest, sizeof(digest));
        s_ota_state = 1U;
        s_ota_expected = expected;
        s_ota_received = 0U;
        mg_iobuf_del(&c->recv, 0, hm->head.len);
    } else if (ev == MG_EV_READ && s_ota_url_state.active) {
        if (c->recv.len > 0U) {
            if (s_ota_url_state.received + c->recv.len > s_ota_url_state.expected ||
                ota_write_chunk((uint32_t)s_ota_url_state.received,
                                (const uint8_t *)c->recv.buf,
                                (uint32_t)c->recv.len) != 0) {
                http_ota_abort_state(&s_ota_url_state);
                s_ota_state = 4U;
                c->is_closing = 1;
                return;
            }
            s_ota_url_state.received += c->recv.len;
            s_ota_received = s_ota_url_state.received;
            mg_iobuf_del(&c->recv, 0, c->recv.len);
            if (s_ota_url_state.received == s_ota_url_state.expected) {
                if (ota_finalize(s_ota_url_state.has_sha256 ? s_ota_url_state.sha256 : NULL) != 0) {
                    http_ota_abort_state(&s_ota_url_state);
                    s_ota_state = 4U;
                    c->is_closing = 1;
                    return;
                }
                s_ota_url_state.active = false;
                s_ota_state = 2U;
                (void)xTaskCreate(ota_commit_task, "OTA_COMMIT", 256U, NULL,
                                  configMAX_PRIORITIES - 2U, NULL);
                c->is_closing = 1;
            }
        }
    } else if (ev == MG_EV_CLOSE && s_ota_url_state.active) {
        http_ota_abort_state(&s_ota_url_state);
        s_ota_state = 4U;
    }
}

static void ota_commit_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(250U));
    (void)ota_commit();
    vTaskDelete(NULL);
}

static void factory_reset_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(250U));
    ota_revert_factory();
    vTaskDelete(NULL);
}

static bool http_ota_content_length(struct mg_http_message *hm, size_t *length)
{
    struct mg_str *header = mg_http_get_header(hm, "Content-Length");
    char value[24];
    if (header == NULL || header->len == 0U || header->len >= sizeof(value)) return false;
    memcpy(value, header->buf, header->len);
    value[header->len] = '\0';
    char *end = NULL;
    unsigned long parsed = strtoul(value, &end, 10);
    if (end == value || *end != '\0' || parsed == 0UL) return false;
    *length = (size_t)parsed;
    return *length <= ota_get_max_fw_size();
}

static bool http_ota_filename_valid(struct mg_http_message *hm)
{
    struct mg_str *header = mg_http_get_header(hm, "X-Firmware-Filename");
    size_t i;
    if (header == NULL || header->len < 4U) return false;
    for (i = 0U; i + 4U == header->len; ++i) {
        if (header->buf[i] == '.' &&
            (header->buf[i + 1U] == 'b' || header->buf[i + 1U] == 'B') &&
            (header->buf[i + 2U] == 'i' || header->buf[i + 2U] == 'I') &&
            (header->buf[i + 3U] == 'n' || header->buf[i + 3U] == 'N')) return true;
    }
    return false;
}

static void http_ota_abort_state(http_ota_state_t *state)
{
    if (state == NULL || state->active || s_ota_url_state.active) ota_abort();
    (void)state;
    memset(&s_ota_upload_state, 0, sizeof(s_ota_upload_state));
    memset(&s_ota_url_state, 0, sizeof(s_ota_url_state));
    s_ota_state = 0U;
    s_ota_expected = 0U;
    s_ota_received = 0U;
}

static bool http_ota_process_stream(struct mg_connection *c, http_ota_state_t *state)
{
    if (c->recv.len == 0U) return true;
    if (state->received + c->recv.len > state->expected) {
        http_ota_abort_state(state);
        mg_http_reply(c, 413, "Content-Type: application/json\r\n", "{\"status\":\"error\",\"message\":\"Firmware exceeds Content-Length\"}");
        c->is_draining = 1;
        return false;
    }
    if (ota_write_chunk((uint32_t)state->received,
                        (const uint8_t *)c->recv.buf,
                        (uint32_t)c->recv.len) != 0) {
        http_ota_abort_state(state);
        mg_http_reply(c, 500, "Content-Type: application/json\r\n", "{\"status\":\"error\",\"message\":\"Firmware write failed\"}");
        c->is_draining = 1;
        return false;
    }
    state->received += c->recv.len;
    s_ota_received = state->received;
    mg_iobuf_del(&c->recv, 0, c->recv.len);
    if (state->received == state->expected) {
        if (ota_finalize(state->sha256) != 0) {
            http_ota_abort_state(state);
            mg_http_reply(c, 422, "Content-Type: application/json\r\n", "{\"status\":\"error\",\"message\":\"Firmware verification failed\"}");
            c->is_draining = 1;
            return false;
        }
        state->active = false;
        s_ota_state = 2U;
        mg_http_reply(c, 200, "Content-Type: application/json\r\n", "{\"status\":\"ok\",\"message\":\"Firmware accepted\"}");
        (void)xTaskCreate(ota_commit_task, "OTA_COMMIT", 256U, NULL,
                          configMAX_PRIORITIES - 2U, NULL);
        memset(state, 0, sizeof(*state));
        c->is_draining = 1;
    }
    return true;
}

/* External functions */
extern int netif_get_num(void);

/* Forward declaration from mongoose_glue */
extern uint64_t mongoose_get_time_ms(void);

/**
 * @brief Get device status as JSON
 */
static const char* get_rs485_owner_str(rs485_owner_t owner) {
    switch (owner) {
        case RS485_OWNER_SERIAL_TCP_SERVER: return "TCP_SERVER";
        case RS485_OWNER_SERIAL_TCP_CLIENT: return "TCP_CLIENT";
        case RS485_OWNER_MODBUS_MASTER: return "MODBUS_MASTER";
        case RS485_OWNER_MODBUS_GATEWAY: return "MODBUS_GATEWAY";
        default: return "NONE";
    }
}
static const char* get_rs485_state_str(rs485_state_t state) {
    switch (state) {
        case RS485_STATE_DISABLED: return "DISABLED";
        case RS485_STATE_STOPPED: return "STOPPED";
        case RS485_STATE_STARTING: return "STARTING";
        case RS485_STATE_RUNNING: return "RUNNING";
        case RS485_STATE_STOPPING: return "STOPPING";
        case RS485_STATE_RECONFIGURING: return "RECONFIGURING";
        case RS485_STATE_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

const char *http_server_get_hostname(void) {
    return s_hostname;
}

static bool http_netif_snapshot(net_if_type_t type, char *ip, size_t ip_len,
                                bool *link, bool *up, bool *usable) {
    struct netif *nif = network_interface_get_netif(type);
    if (nif == NULL) return false;
    ip4addr_ntoa_r(netif_ip4_addr(nif), ip, ip_len);
    *link = netif_is_link_up(nif);
    *up = netif_is_up(nif);
    *usable = *link && *up && !ip4_addr_isany_val(*netif_ip4_addr(nif));
    return true;
}

static void http_discovery_json(struct mg_connection *c) {
    char eth_ip[16] = "0.0.0.0", wifi_ip[16] = "0.0.0.0";
    bool eth_link = false, eth_up = false, eth_ok = false;
    bool wifi_link = false, wifi_up = false, wifi_ok = false;
    (void)http_netif_snapshot(NET_IF_ETH, eth_ip, sizeof(eth_ip), &eth_link, &eth_up, &eth_ok);
    (void)http_netif_snapshot(NET_IF_WIFI, wifi_ip, sizeof(wifi_ip), &wifi_link, &wifi_up, &wifi_ok);
    mg_http_reply(c, 200, "Content-Type: application/json\r\n",
        "{\"hostname\":\"%s\",\"mdns_url\":\"http://%s.local/\",\"http_port\":80,"
        "\"interfaces\":[{\"name\":\"ETH\",\"ip\":\"%s\",\"link\":%s,\"up\":%s,\"usable\":%s,\"url\":\"http://%s/\"},"
        "{\"name\":\"WIFI\",\"ip\":\"%s\",\"link\":%s,\"up\":%s,\"usable\":%s,\"url\":\"http://%s/\"}],"
        "\"usb_url\":\"http://192.168.7.1/\",\"usb_available\":%s,\"mdns_available\":%s}",
        s_hostname, s_hostname, eth_ip, eth_link ? "true":"false", eth_up ? "true":"false", eth_ok ? "true":"false", eth_ip,
        wifi_ip, wifi_link ? "true":"false", wifi_up ? "true":"false", wifi_ok ? "true":"false", wifi_ip,
        usb_netif_is_available() ? "true":"false",
        (s_mdns != NULL && s_mdns_ip != 0U) ? "true":"false");
}

static void http_capabilities_json(struct mg_connection *c)
{
    mg_http_reply(c, 200, "Content-Type: application/json\r\n",
        "{\"schema_version\":1,\"contract_version\":\"%s\",\"firmware_api\":1,"
        "\"features\":{\"serial_tcp_server\":true,\"serial_tcp_client\":false,"
        "\"modbus_master\":false,\"modbus_gateway\":false,\"mqtt_publishers\":false}}",
        config_get_contract_version());
}

#if GW_MDNS_ENABLE
static void http_mdns_handler(struct mg_connection *c, int ev, void *ev_data) {
    if (ev == MG_EV_MDNS_A) {
        struct mg_mdns_req *req = (struct mg_mdns_req *)ev_data;
        if (s_mdns_ip == 0U) req->is_resp = false;
    } else if (ev == MG_EV_MDNS_PTR || ev == MG_EV_MDNS_SRV || ev == MG_EV_MDNS_TXT) {
        static struct mg_dnssd_record record;
        static const char service[] = "_http._tcp";
        static const char txt[] = "path=/";
        struct mg_mdns_req *req = (struct mg_mdns_req *)ev_data;
        record.srvcproto = mg_str(service);
        record.txt = mg_str(txt);
        record.port = HTTP_SERVER_PORT;
        req->r = &record;
        req->respname = mg_str(s_hostname);
        req->is_resp = s_mdns_ip != 0U;
    }
    (void)c;
}
#endif

static void http_mdns_refresh(void) {
    struct netif *nif = NULL;
    bool ok = false;
    struct netif *eth = network_interface_get_netif(NET_IF_ETH);
    struct netif *wifi = network_interface_get_netif(NET_IF_WIFI);
    if (eth == NULL || !netif_is_up(eth) || !netif_is_link_up(eth) ||
        ip4_addr_isany_val(*netif_ip4_addr(eth))) eth = NULL;
    if (wifi == NULL || !netif_is_up(wifi) || !netif_is_link_up(wifi) ||
        ip4_addr_isany_val(*netif_ip4_addr(wifi))) wifi = NULL;
    if (eth && netif_is_up(eth) && netif_is_link_up(eth) && !ip4_addr_isany_val(*netif_ip4_addr(eth))) nif = eth;
    else if (wifi && netif_is_up(wifi) && netif_is_link_up(wifi) && !ip4_addr_isany_val(*netif_ip4_addr(wifi))) nif = wifi;
    if (nif) { s_mdns_ip = netif_ip4_addr(nif)->addr; ok = true; }
    else { s_mdns_ip = 0U; }
    if (s_mdns && ok) {
        /* mg_mdns_listen() owns the multicast membership for the lifetime of
         * this socket.  It joins with INADDR_ANY, which lets lwIP maintain
         * membership on every IGMP-capable interface.  Do not drop/re-add the
         * membership here: doing so races the periodic IGMP timer when an
         * interface obtains or loses its address and can leave a freed group
         * node visible to igmp_tmr(). */
        memcpy(s_mdns->data, &s_mdns_ip, sizeof(s_mdns_ip));
    }
    if (s_mdns && !ok) {
        memset(s_mdns->data, 0, sizeof(s_mdns_ip));
    }
    if (ok && s_mdns != NULL && !s_mdns_ready_logged) {
        LOGI(TAG, "Web Config ready: http://%s.local/", s_hostname);
        s_mdns_ready_logged = true;
    } else if (!ok) s_mdns_ready_logged = false;

    struct netif *ready[] = {eth, wifi};
    bool *logged[] = {&s_eth_ready_logged, &s_wifi_ready_logged};
    const char *names[] = {"ETH", "WIFI"};
    for (size_t i = 0U; i < 2U; ++i) {
        if (ready[i] != NULL && !*logged[i]) {
            char ip[16];
            ip4addr_ntoa_r(netif_ip4_addr(ready[i]), ip, sizeof(ip));
            LOGI(TAG, "Web Config interface: %s http://%s/", names[i], ip);
            *logged[i] = true;
        } else if (ready[i] == NULL) {
            *logged[i] = false;
        }
    }
    if (!ok && usb_netif_is_available() && !s_usb_ready_logged) {
        LOGI(TAG, "Web Config available via USB: http://192.168.7.1/");
        s_usb_ready_logged = true;
    } else if (ok || !usb_netif_is_available()) {
        s_usb_ready_logged = false;
    }
}

/**
 * @brief Get current config as JSON
 */
char* http_server_get_config_json(void) {
    return config_get_json_string();
}

/**
 * @brief Apply config from JSON
 */
int http_server_set_config_json(const char *json) {
    LOGI(TAG, "Config update requested from Web UI");
    return config_update_from_json(json);
}

/**
 * @brief Reboot task for delayed reboot
 */
static void http_server_reboot_task(void *params) {
    (void)params;
    vTaskDelay(pdMS_TO_TICKS(1000));
    NVIC_SystemReset();
    vTaskDelete(NULL);
}

/**
 * @brief Reboot device with delay
 */
void http_server_reboot(void) {
    LOGI(TAG, "Reboot requested via Web UI. Rebooting in 1s...");
    xTaskCreate(http_server_reboot_task, "REBOOT", 128, NULL, configMAX_PRIORITIES - 1, NULL);
}

static bool is_authorized(struct mg_http_message *hm) {
    char user[32] = {0};
    char pass[32] = {0};
    mg_http_creds(hm, user, sizeof(user), pass, sizeof(pass));
    
    sys_config_lock();
    bool admin_ok = (strcmp(user, "admin") == 0 && 
                     strlen(g_sys_config.security.admin_password) > 0 && 
                     strcmp(pass, g_sys_config.security.admin_password) == 0);
    sys_config_unlock();

    /* If password not set, allow access by default for setup */
    sys_config_lock();
    bool no_password = (strlen(g_sys_config.security.admin_password) == 0);
    sys_config_unlock();

    return (admin_ok || no_password);
}

/**
 * @brief HTTP request handler
 */
static void http_handler(struct mg_connection *c, int ev, void *ev_data) {
    http_connection_state_t *connection_state =
        (http_connection_state_t *)c->data;

    if (ev == MG_EV_ACCEPT && !connection_state->connection_counted) {
        if (s_http_active_connections >= HTTP_MAX_ACTIVE_CONNECTIONS) {
            c->is_closing = 1;
            return;
        }
        connection_state->connection_counted = true;
        s_http_active_connections++;
    } else if (ev == MG_EV_CLOSE && connection_state->connection_counted) {
        connection_state->connection_counted = false;
        if (s_http_active_connections > 0U) s_http_active_connections--;
    }

    if (ev == MG_EV_HTTP_HDRS) {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;
        bool admin_post = mg_strcmp(hm->method, mg_str("POST")) == 0 &&
            (mg_match(hm->uri, mg_str("/api/reboot"), NULL) ||
             mg_match(hm->uri, mg_str("/api/factory_reset"), NULL) ||
             mg_match(hm->uri, mg_str("/api/ota_url"), NULL) ||
             mg_match(hm->uri, mg_str("/api/ota/abort"), NULL));
        if (admin_post && !is_authorized(hm)) {
            mg_http_reply(c, 401, "WWW-Authenticate: Basic realm=\"GatewayV2\"\r\nContent-Type: application/json\r\n",
                          "{\"status\":\"error\",\"message\":\"Unauthorized\"}");
            return;
        }
        if (mg_strcmp(hm->method, mg_str("POST")) == 0 &&
            (mg_match(hm->uri, mg_str("/api/config"), NULL) || mg_match(hm->uri, mg_str("/api/v1/config"), NULL))) {
            struct mg_str *length_header = mg_http_get_header(hm, "Content-Length");
            if (length_header != NULL && length_header->len > 0U) {
                char length_buf[16];
                if (length_header->len >= sizeof(length_buf)) {
                    mg_http_reply(c, 413, "Content-Type: application/json\r\n", "{\"status\":\"error\",\"message\":\"Config body too large\"}");
                    c->is_draining = 1;
                    return;
                }
                memcpy(length_buf, length_header->buf, length_header->len);
                length_buf[length_header->len] = '\0';
                unsigned long body_len = strtoul(length_buf, NULL, 10);
                if (body_len > HTTP_CONFIG_MAX_BODY) {
                    mg_http_reply(c, 413, "Content-Type: application/json\r\n", "{\"status\":\"error\",\"message\":\"Config body too large\"}");
                    c->is_draining = 1;
                    return;
                }
            }
        }
        if (mg_strcmp(hm->method, mg_str("POST")) == 0 &&
            mg_match(hm->uri, mg_str("/api/firmware_update"), NULL)) {
            size_t expected = 0U;
            uint8_t digest[32];
            if (!is_authorized(hm)) {
                mg_http_reply(c, 401, "WWW-Authenticate: Basic realm=\"GatewayV2\"\r\nContent-Type: application/json\r\n", "{\"status\":\"error\",\"message\":\"Unauthorized\"}");
                return;
            }
            if (!http_ota_content_length(hm, &expected) || !http_ota_filename_valid(hm) ||
                !http_ota_sha256(hm, digest) ||
                ota_begin((uint32_t)expected, 0U) != 0) {
                mg_http_reply(c, 400, "Content-Type: application/json\r\n", "{\"status\":\"error\",\"message\":\"Invalid firmware size or OTA is busy\"}");
                c->is_draining = 1;
                return;
            }
            memset(&s_ota_upload_state, 0, sizeof(s_ota_upload_state));
            s_ota_upload_state.kind = 1U;
            s_ota_upload_state.active = true;
            s_ota_upload_state.expected = expected;
            s_ota_upload_state.owner_connection_id = c->id;
            memcpy(s_ota_upload_state.sha256, digest, sizeof(digest));
            s_ota_upload_state.has_sha256 = true;
            s_ota_state = 1U;
            s_ota_expected = expected;
            s_ota_received = 0U;
            mg_iobuf_del(&c->recv, 0, hm->head.len);
            c->pfn = NULL;
            mg_call(c, MG_EV_READ, &c->recv.len);
            return;
        }
    }

    if ((ev == MG_EV_READ || ev == MG_EV_CLOSE) &&
        s_ota_upload_state.active &&
        s_ota_upload_state.owner_connection_id == c->id) {
        if (ev == MG_EV_READ) {
            (void)http_ota_process_stream(c, &s_ota_upload_state);
        } else {
            http_ota_abort_state(&s_ota_upload_state);
        }
        return;
    }

    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;

        /* Require auth for all POST requests under /api */
        if (mg_match(hm->uri, mg_str("/api/#"), NULL) && mg_strcmp(hm->method, mg_str("POST")) == 0) {
            if (!is_authorized(hm)) {
                mg_http_reply(c, 401, "WWW-Authenticate: Basic realm=\"GatewayV2\"\r\nContent-Type: application/json\r\n", 
                              "{\"status\":\"error\",\"message\":\"Unauthorized\"}");
                return;
            }
        }

        /* Route: GET /api/status */
        if (mg_match(hm->uri, mg_str("/api/status"), NULL) || mg_match(hm->uri, mg_str("/api/v1/status"), NULL)) {
            uint32_t total_mem = configTOTAL_HEAP_SIZE;
            uint32_t free_mem = xPortGetFreeHeapSize();
            uint32_t used_mem = total_mem > free_mem ? total_mem - free_mem : 0U;

            network_probe_stats_t probe_stats;
            network_get_probe_stats(&probe_stats);

            net_if_type_t active = network_get_active_interface();
            const char *active_name = "none";
            switch (active) {
                case NET_IF_ETH: active_name = "eth"; break;
                case NET_IF_WIFI: active_name = "wifi"; break;
                case NET_IF_LTE: active_name = "lte"; break;
                default: active_name = "none"; break;
            }

            char eth_ip[16], wifi_ip[16], lte_ip[16];
            strncpy(eth_ip, network_get_ip_address(NET_IF_ETH), sizeof(eth_ip)-1); eth_ip[15]='\0';
            strncpy(wifi_ip, network_get_ip_address(NET_IF_WIFI), sizeof(wifi_ip)-1); wifi_ip[15]='\0';
            strncpy(lte_ip, network_get_ip_address(NET_IF_LTE), sizeof(lte_ip)-1); lte_ip[15]='\0';
            
            bool eth_up = (network_interface_get_status(NET_IF_ETH) == NET_IF_STATUS_UP);
            bool wifi_up = (network_interface_get_status(NET_IF_WIFI) == NET_IF_STATUS_UP);
            bool lte_up = (network_interface_get_status(NET_IF_LTE) == NET_IF_STATUS_UP);

            mg_printf(c, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nTransfer-Encoding: chunked\r\n\r\n");
            
            mg_http_printf_chunk(c, 
                "{"
                "\"api_version\":1,\"schema_version\":1,"\
                "\"state\":\"%s\","\
                "\"uptime\":%lu,"
                "\"memory\":{\"total\":%lu,\"free\":%lu,\"used\":%lu,\"minimum_free\":%lu},"
                "\"failover\":{\"probe_ok\":%lu,\"probe_fail\":%lu,\"suspected\":\"%s\"},"
                "\"network\":{"
                    "\"active\":\"%s\","
                    "\"eth\":{\"ip\":\"%s\",\"connected\":%s},"
                    "\"wifi\":{\"ip\":\"%s\",\"connected\":%s},"
                    "\"lte\":{\"ip\":\"%s\",\"connected\":%s}"
                "}"
                "}",
                gateway_state_str(gateway_get_state()),\
                  (uint32_t)(HAL_GetTick() / 1000), total_mem, free_mem, used_mem, (uint32_t)xPortGetMinimumEverFreeHeapSize(),
                (unsigned long)probe_stats.probe_ok, (unsigned long)probe_stats.probe_fail, probe_stats.suspected,
                active_name,
                eth_ip, eth_up ? "true" : "false",
                wifi_ip, wifi_up ? "true" : "false",
                lte_ip, lte_up ? "true" : "false"
            );
            
            mg_http_printf_chunk(c, "");
            return;
        }

        /* Route: GET /api/config/apply_status
         * Lets the dashboard poll the async apply pipeline after a save so a
         * failed node (module + error code) becomes visible and retryable. */
        if (mg_match(hm->uri, mg_str("/api/config/apply_status"), NULL)) {
            config_apply_status_t apply_st;
            config_apply_manager_get_status(&apply_st);
            mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                          "{\"generation\":%lu,\"applied_generation\":%lu,\"state\":\"%s\",\"last_error_module\":\"%s\",\"last_error_code\":\"%s\"}",
                          (unsigned long)apply_st.current_generation,
                          (unsigned long)apply_st.applied_generation,
                          config_apply_state_to_string(apply_st.state),
                          apply_st.last_error.module,
                          apply_st.last_error.code);
            return;
        }

        /* Route: GET /api/rs485/status */
        if (mg_match(hm->uri, mg_str("/api/rs485/status"), NULL)) {
            mg_printf(c, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nTransfer-Encoding: chunked\r\n\r\n");
            mg_http_printf_chunk(c, "[");
            for (int i = 0; i < RS485_MAX_PORTS; i++) {
                rs485_port_status_t pstat;
                memset(&pstat, 0, sizeof(pstat));
                rs485_port_get_status((uint8_t)i, &pstat);
                
                mg_http_printf_chunk(c, "%s"
                    "{\"port\":%d,"
                    "\"owner\":\"%s\","
                    "\"state\":\"%s\","
                    "\"counters\":{"
                        "\"rx_bytes\":%lu,"
                        "\"tx_bytes\":%lu,"
                        "\"hw_errors\":%lu,"
                        "\"last_activity_ms\":%lu"
                    "}}",
                    (i > 0) ? "," : "",
                    i,
                    get_rs485_owner_str(pstat.owner),
                    get_rs485_state_str(pstat.state),
                    pstat.counters.rx_bytes,
                    pstat.counters.tx_bytes,
                    pstat.counters.hw_errors,
                    pstat.counters.last_activity_ms
                );
            }
            mg_http_printf_chunk(c, "]");
            mg_http_printf_chunk(c, "");
            return;
        }

        /* Route: GET /api/mqtt/status */
        if (mg_match(hm->uri, mg_str("/api/mqtt/status"), NULL)) {
            mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                "{"
                "\"connected\":%s,"
                "\"state\":\"%s\","
                "\"dns_failures\":%lu,"
                "\"connect_timeouts\":%lu,"
                "\"last_error\":%d,"
                "\"dropped_uart_frames\":%lu"
                "}",
                app_mqtt_is_connected() ? "true" : "false",
                app_mqtt_state_name(),
                app_mqtt_dns_failures(),
                app_mqtt_connect_timeouts(),
                app_mqtt_last_error(),
                app_mqtt_dropped_uart_frames()
            );
            return;
        }

        if (mg_strcmp(hm->method, mg_str("GET")) == 0 &&
            (mg_match(hm->uri, mg_str("/api/capabilities"), NULL) ||
             mg_match(hm->uri, mg_str("/api/v1/capabilities"), NULL))) {
            http_capabilities_json(c);
            return;
        }

        if (mg_strcmp(hm->method, mg_str("GET")) == 0 &&
            (mg_match(hm->uri, mg_str("/api/network/discovery"), NULL) || mg_match(hm->uri, mg_str("/api/v1/network/discovery"), NULL))) {
            http_discovery_json(c);
            return;
        }

        /* Route: GET /api/telemetry/snapshot
         * One-shot JSON array of the freshest telemetry records so the
         * dashboard can poll a bounded document instead of consuming the
         * infinite chunked stream from JavaScript. */
        if (mg_match(hm->uri, mg_str("/api/telemetry/snapshot"), NULL)) {
            TickType_t now_ticks = xTaskGetTickCount();
            static const char *quality_names[] = {
                "invalid", "good", "stale", "timeout", "error"
            };
            bool first = true;

            mg_printf(c, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nTransfer-Encoding: chunked\r\n\r\n");
            mg_http_printf_chunk(c, "[");
            for (telemetry_id_t i = 0; i < 64U; i++) {
                const char *key = telemetry_store_get_key(i);
                if (key == NULL || key[0] == '\0') continue;

                telemetry_point_snapshot_t snap;
                if (telemetry_store_get(i, &snap) != 0) continue;
                if (snap.quality == TELEMETRY_VALUE_INVALID) continue;

                /* JSON-escape the user-defined data key. */
                char kbuf[160];
                size_t ko = 0U;
                for (const char *p = key; *p != '\0' && ko + 6U < sizeof(kbuf); ++p) {
                    unsigned char ch = (unsigned char)*p;
                    if (ch == '"' || ch == '\\') {
                        kbuf[ko++] = '\\';
                        kbuf[ko++] = (char)ch;
                    } else if (ch == '\n') {
                        kbuf[ko++] = '\\'; kbuf[ko++] = 'n';
                    } else if (ch == '\r') {
                        kbuf[ko++] = '\\'; kbuf[ko++] = 'r';
                    } else if (ch == '\t') {
                        kbuf[ko++] = '\\'; kbuf[ko++] = 't';
                    } else if (ch >= 0x20U) {
                        kbuf[ko++] = (char)ch;
                    }
                }
                kbuf[ko] = '\0';

                char vbuf[32];
                switch (snap.value.type) {
                case TELEMETRY_TYPE_INT32:
                    snprintf(vbuf, sizeof(vbuf), "%ld", (long)snap.value.value.i32);
                    break;
                case TELEMETRY_TYPE_UINT32:
                    snprintf(vbuf, sizeof(vbuf), "%lu",
                             (unsigned long)snap.value.value.u32);
                    break;
                case TELEMETRY_TYPE_FLOAT:
                    snprintf(vbuf, sizeof(vbuf), "%.4g", (double)snap.value.value.f32);
                    break;
                default:
                    snprintf(vbuf, sizeof(vbuf), "%s",
                             snap.value.value.boolean ? "true" : "false");
                    break;
                }

                const char *quality =
                    quality_names[snap.quality <= TELEMETRY_VALUE_ERROR
                                      ? (int)snap.quality : 0];
                uint32_t age = (snap.timestamp <= now_ticks)
                                   ? ((now_ticks - snap.timestamp) / 1000U)
                                   : 0U;
                mg_http_printf_chunk(c, "%s{\"k\":\"%s\",\"v\":%s,\"q\":\"%s\",\"age\":%lu}",
                                     first ? "" : ",", kbuf, vbuf, quality,
                                     (unsigned long)age);
                first = false;
            }
            mg_http_printf_chunk(c, "]");
            mg_http_printf_chunk(c, "");
            return;
        }

        /* Route: GET /api/telemetry */
        if (mg_match(hm->uri, mg_str("/api/telemetry"), NULL) || mg_match(hm->uri, mg_str("/api/v1/telemetry"), NULL)) {
            mg_printf(c, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nTransfer-Encoding: chunked\r\n\r\n");
            mg_http_printf_chunk(c, "{\"ts\":%lu,\"data\":{", ntp_get_unix_time());
            
            bool first = true;
            for (telemetry_id_t i = 0; i < 256; i++) {
                const char *key = telemetry_store_get_key(i);
                if (!key) break;
                
                telemetry_point_snapshot_t snap;
                if (telemetry_store_get(i, &snap) == 0) {
                    if (snap.quality == TELEMETRY_VALUE_INVALID) continue;
                    
                    if (!first) {
                        mg_http_printf_chunk(c, ",");
                    }
                    first = false;
                    
                    if (snap.value.type == TELEMETRY_TYPE_FLOAT) {
                        mg_http_printf_chunk(c, "\"%s\":%.2f", key, snap.value.value.f32);
                    } else if (snap.value.type == TELEMETRY_TYPE_INT32) {
                        mg_http_printf_chunk(c, "\"%s\":%ld", key, snap.value.value.i32);
                    } else if (snap.value.type == TELEMETRY_TYPE_UINT32) {
                        mg_http_printf_chunk(c, "\"%s\":%lu", key, snap.value.value.u32);
                    } else if (snap.value.type == TELEMETRY_TYPE_BOOL) {
                        mg_http_printf_chunk(c, "\"%s\":%s", key, snap.value.value.boolean ? "true" : "false");
                    }
                }
            }
            
            mg_http_printf_chunk(c, "}}");
            mg_http_printf_chunk(c, "");
            return;
        }

        /* Route: GET /api/config */
        if (mg_strcmp(hm->method, mg_str("GET")) == 0 && (mg_match(hm->uri, mg_str("/api/config"), NULL) || mg_match(hm->uri, mg_str("/api/v1/config"), NULL))) {
            char *json = http_server_get_config_json();
            if (json) {
                mg_printf(c,
                          "HTTP/1.1 200 OK\r\n"
                          "Content-Type: application/json\r\n"
                          "Content-Length: %d\r\n"
                          "\r\n",
                          (int)strlen(json));
                mg_send(c, json, strlen(json));
                cJSON_free(json); // Use cJSON_free as it was allocated by cJSON
            } else {
                mg_http_reply(c, 500, "Content-Type: application/json\r\n", "{\"status\":\"error\"}");
            }
            return;
        }

        /* Route: POST /api/config */
        if (mg_strcmp(hm->method, mg_str("POST")) == 0 && (mg_match(hm->uri, mg_str("/api/config"), NULL) || mg_match(hm->uri, mg_str("/api/v1/config"), NULL))) {
            if (hm->body.len > HTTP_CONFIG_MAX_BODY) {
                mg_http_reply(c, 413, "Content-Type: application/json\r\n", "{\"status\":\"error\",\"message\":\"Config body too large\"}");
                return;
            }
            uint32_t heap_before = xPortGetFreeHeapSize();
            int res = config_update_from_json_n(hm->body.buf, hm->body.len);
            LOGI(TAG, "[CONFIG] body=%lu heap_before=%lu heap_after=%lu min_free=%lu",
                 (unsigned long)hm->body.len,
                 (unsigned long)heap_before,
                 (unsigned long)xPortGetFreeHeapSize(),
                 (unsigned long)xPortGetMinimumEverFreeHeapSize());
            if (res == 0) {
                config_apply_status_t apply_st;
                config_apply_manager_get_status(&apply_st);
                mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                              "{\"status\":\"ok\",\"message\":\"Config saved\",\"schema_version\":1,\"contract_version\":\"%s\",\"generation\":%lu,\"saved\":true,\"applied\":%s,\"applied_generation\":%lu,\"apply_state\":\"%s\",\"reboot_required\":false,\"last_error_module\":\"%s\",\"last_error_code\":\"%s\"}",
                              config_get_contract_version(), (unsigned long)config_get_generation(), apply_st.state == APPLY_STATE_APPLIED ? "true" : "false", (unsigned long)apply_st.applied_generation, config_apply_state_to_string(apply_st.state), apply_st.last_error.module, apply_st.last_error.code);
            } else if (res == CONFIG_RESULT_CONFLICT) {
                mg_http_reply(c, 409, "Content-Type: application/json\r\n",
                              "{\"status\":\"error\",\"error\":\"conflict\",\"path\":\"%s\",\"code\":\"%s\",\"message\":\"%s\"}",
                              config_get_last_error_path(), config_get_last_error_code(), config_get_last_error_message());
            } else if (res == CONFIG_RESULT_UNSUPPORTED) {
                mg_http_reply(c, 422, "Content-Type: application/json\r\n",
                              "{\"status\":\"error\",\"error\":\"unsupported_feature\",\"path\":\"%s\",\"code\":\"%s\",\"message\":\"%s\"}",
                              config_get_last_error_path(), config_get_last_error_code(), config_get_last_error_message());
            } else {
                mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                              "{\"status\":\"error\",\"error\":\"validation_failed\",\"path\":\"%s\",\"code\":\"%s\",\"message\":\"%s\"}",
                              config_get_last_error_path(), config_get_last_error_code(), config_get_last_error_message());
            }
            return;
        }

        
        if (mg_strcmp(hm->method, mg_str("POST")) == 0 &&
            mg_match(hm->uri, mg_str("/api/config/apply/retry"), NULL)) {
            config_apply_manager_retry();
            mg_http_reply(c, 200, "Content-Type: application/json\r\n", "{\"status\":\"ok\",\"message\":\"Retry scheduled\"}");
            return;
        }

        /* Route: POST /api/reboot */
        if (mg_strcmp(hm->method, mg_str("POST")) == 0 &&
            mg_match(hm->uri, mg_str("/api/reboot"), NULL)) {
            mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                "{\"status\":\"ok\",\"message\":\"Rebooting...\"}");
            http_server_reboot();
            return;
        }

        if (mg_strcmp(hm->method, mg_str("POST")) == 0 &&
            mg_match(hm->uri, mg_str("/api/factory_reset"), NULL)) {
            mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                          "{\"status\":\"ok\",\"message\":\"Factory reset scheduled\"}");
            (void)xTaskCreate(factory_reset_task, "FACTORY_RST", 256U, NULL,
                              configMAX_PRIORITIES - 2U, NULL);
            return;
        }

        if (mg_match(hm->uri, mg_str("/api/ota/status"), NULL) || mg_match(hm->uri, mg_str("/api/v1/ota/status"), NULL)) {
            ota_status_t status;
            unsigned progress;
            ota_get_status(&status);
            progress = status.expected != 0U
                ? (unsigned)((status.received * 100U) / status.expected) : 0U;
            mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                          "{\"state\":\"%s\",\"received\":%lu,\"expected\":%lu,\"progress\":%u,\"version\":%lu,\"error_code\":%ld,\"error\":\"\"}",
                          http_ota_state_name(status.state),
                          (unsigned long)status.received,
                          (unsigned long)status.expected,
                          progress,
                          (unsigned long)status.version,
                          (long)status.error_code);
            return;
        }

        if (mg_strcmp(hm->method, mg_str("POST")) == 0 &&
            mg_match(hm->uri, mg_str("/api/ota/abort"), NULL)) {
            http_ota_abort_state(NULL);
            mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                          "{\"status\":\"ok\",\"message\":\"OTA aborted\"}");
            return;
        }

        if (mg_strcmp(hm->method, mg_str("POST")) == 0 &&
            mg_match(hm->uri, mg_str("/api/ota_url"), NULL)) {
            char *url = mg_json_get_str(hm->body, "$.url");
            bool valid = url != NULL &&
                         (strncmp(url, "http://", 7U) == 0 ||
                          strncmp(url, "https://", 8U) == 0) &&
                         strchr(url, '\r') == NULL && strchr(url, '\n') == NULL;
            if (!valid || s_ota_state != 0U) {
                if (url != NULL) mg_free(url);
                mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                              "{\"status\":\"error\",\"message\":\"Invalid URL or OTA already active\"}");
                return;
            }
            /* Reserve the single OTA slot before starting the asynchronous
             * connection.  Otherwise two URL requests can pass the busy
             * check before MG_EV_HTTP_HDRS arrives. */
            s_ota_state = 3U;
            struct mg_connection *download = mg_http_connect(&s_mgr, url,
                                                               http_ota_url_cb, NULL);
            mg_free(url);
            if (download == NULL) {
                http_ota_abort_state(NULL);
                mg_http_reply(c, 502, "Content-Type: application/json\r\n",
                              "{\"status\":\"error\",\"message\":\"Unable to start firmware download\"}");
                return;
            }
            mg_http_reply(c, 202, "Content-Type: application/json\r\n",
                          "{\"status\":\"ok\",\"message\":\"Firmware download started\"}");
            return;
        }

        /* Route: POST /api/fs/upload - Upload file to LittleFS */
        if (mg_match(hm->uri, mg_str("/api/fs/upload"), NULL)) {
            long upload_result = mg_http_upload(c, hm, mongoose_lfs_get_fs(), "/web_root", 1024 * 1024 * 2); // 2MB max
            if (upload_result < 0) {
                LOGW(TAG, "LittleFS upload failed: result=%ld body=%lu", upload_result,
                     (unsigned long)hm->body.len);
            }
            return;
        }

        /* Serve static files from LittleFS */
        struct mg_http_serve_opts opts = {
            .root_dir = "/web_root",
            .mime_types = "",
            .extra_headers = "",
            .fs = mongoose_lfs_get_fs()
        };
        if (mg_strcmp(hm->uri, mg_str("/")) == 0) {
            mg_http_serve_file(c, hm, "/web_root/dashboard_v16.html", &opts);
        } else {
            mg_http_serve_dir(c, hm, &opts);
        }
    }
}

/**
 * @brief HTTP Server task
 */
void http_server_task(void *params) {
    (void)params;

    LOGI(TAG, "HTTP Server task started");

    /* Wait for lwIP to be fully ready before using networking APIs */
    network_wait_lwip_ready();

    uint8_t mac[6] = {0};
    eth_driver_get_mac(mac);
    /* Shared product hostname for the single-gateway deployment profile. */
    (void)mac;
    snprintf(s_hostname, sizeof(s_hostname), "synaptix-gateway");

    /* Ensure /web_root exists in LittleFS */
    mongoose_lfs_get_fs()->mkd("/web_root");

    /* Initialize mongoose manager */
    mg_mgr_init(&s_mgr);

    /* Create HTTP listener */
    char listen_addr[32];
    snprintf(listen_addr, sizeof(listen_addr), "http://0.0.0.0:%d", HTTP_SERVER_PORT);

    struct mg_connection *c = mg_http_listen(&s_mgr, listen_addr, http_handler, NULL);
    if (c == NULL) {
        LOGE(TAG, "Failed to create HTTP listener on port %d", HTTP_SERVER_PORT);
        vTaskDelete(NULL);
        return;
    }

#if GW_MDNS_ENABLE
    s_mdns = mg_mdns_listen(&s_mgr, http_mdns_handler, s_hostname);
    if (s_mdns == NULL) {
        LOGW(TAG, "mDNS listener unavailable; IP fallback remains active");
    }
#else
    LOGI(TAG, "mDNS disabled for regression isolation; IP fallback remains active");
#endif
    http_mdns_refresh();

    LOGI(TAG, "HTTP Server listening on http://0.0.0.0:%d", HTTP_SERVER_PORT);
    LOGI(TAG, "Web Config access: use the ETH/WiFi IP shown when the interface is UP");

    /* Main loop */
    while (1) {
        http_mdns_refresh();
        mg_mgr_poll(&s_mgr, 100);  /* Poll every 100ms */
        vTaskDelay(pdMS_TO_TICKS(10));  /* Yield CPU */
    }
}

/**
 * @brief Initialize HTTP server
 */
void http_server_init(void) {
    LOGI(TAG, "Initializing HTTP Server...");

    /* Register custom time function */
    /* Note: This is done via mongoose_config.h MG_ENABLE_CUSTOM_MILLIS */

    /* Create HTTP server task */
    xTaskCreateStatic(
        http_server_task,
        "HTTP",
        4096,
        NULL,
        5,
        http_task_stack,
        &http_task_tcb
    );
}
