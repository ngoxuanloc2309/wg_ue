/**
 * @file mqtt_client.c
 * @brief Generic MQTT Client implementation
 */
#include "mqtt_client.h"
#include "logger.h"
#include "FreeRTOS.h"
#include "task.h"
#include "config_manager.h"
#include "lwip/apps/mqtt.h"
#include "lwip/dns.h"
#include "lwip/tcpip.h"
#include "lwip/netif.h"
#include "event_groups.h"
#include "app_config.h"
#include "semphr.h"
#include "network_manager.h"
#include "wifi_driver.h"

static const char *TAG = "MQTT_Client";
static mqtt_client_t *s_mqtt_client = NULL;
static volatile bool s_mqtt_connected = false;
static volatile bool s_mqtt_connecting = false;
static volatile bool s_mqtt_reconnect_requested = false;
static volatile bool s_mqtt_reconnect_pending = false;
static volatile bool s_mqtt_connection_failed = false;
static volatile mqtt_connection_status_t s_mqtt_last_status = MQTT_CONNECT_DISCONNECTED;
static TaskHandle_t s_mqtt_task = NULL;
static TickType_t s_mqtt_connect_started_at = 0;

typedef struct {
    char topic[MQTT_OFFLINE_TOPIC_SIZE];
    char payload[MQTT_OFFLINE_PAYLOAD_SIZE];
} mqtt_offline_item_t;
static mqtt_offline_item_t s_offline_queue[MQTT_OFFLINE_QUEUE_SIZE];
static uint32_t s_offline_count = 0U;
static uint32_t s_offline_dropped = 0U;
static SemaphoreHandle_t s_offline_mutex = NULL;

#define MQTT_RECONNECT_MIN_MS       1000U
#define MQTT_RECONNECT_NORMAL_MS    5000U
#define MQTT_RECONNECT_MAX_MS      60000U
#define MQTT_CONNECT_TIMEOUT_MS    15000U
#define MQTT_ROUTE_DEBOUNCE_MS      500U
#define MQTT_DNS_QUARANTINE_MS    60000U

typedef enum {
    MQTT_STATE_IDLE = 0,
    MQTT_STATE_DNS_PENDING,
    MQTT_STATE_CONNECTING,
    MQTT_STATE_CONNECTED,
    MQTT_STATE_DISCONNECTING,
    MQTT_STATE_BACKOFF
} mqtt_state_t;
static volatile mqtt_state_t s_mqtt_state = MQTT_STATE_IDLE;
static uint32_t s_mqtt_connect_attempt = 0U;
static uint32_t s_mqtt_dns_failures = 0U;
static uint32_t s_mqtt_connect_timeouts = 0U;
static uint32_t s_mqtt_stale_callbacks = 0U;
static int s_mqtt_last_error = 0;

static uint8_t mqtt_config_qos(void)
{
    uint8_t qos;
    sys_config_lock();
    qos = g_sys_config.mqtt.qos <= 2U ? g_sys_config.mqtt.qos : 1U;
    sys_config_unlock();
    return qos;
}

static bool mqtt_config_retain(void)
{
    bool retain;
    sys_config_lock();
    retain = g_sys_config.mqtt.publish_retain;
    sys_config_unlock();
    return retain;
}

/* ---- SimplePLC port hooks (additive; see mqtt_client.h) --------------------
 * The override is written by ports/splc_port (task SPLC) and read here by the
 * MQTT_RECONN task, always as a whole copy inside a short critical section. */
static mqtt_config_override_t s_override;                /* zero = inactive */
static volatile uint32_t s_connected_override_gen = 0U;  /* gen of the live connection */

void mqtt_client_set_override(const mqtt_config_override_t *ov)
{
    if (ov == NULL) return;
    taskENTER_CRITICAL();
    s_override = *ov;
    taskEXIT_CRITICAL();
}

static void mqtt_override_get(mqtt_config_override_t *out)
{
    taskENTER_CRITICAL();
    *out = s_override;
    taskEXIT_CRITICAL();
}

uint32_t mqtt_client_connected_override_generation(void)
{
    return s_connected_override_gen;
}

static ip_addr_t s_resolved_ip;
static bool s_dns_resolved = false;
static bool s_dns_in_progress = false;
static uint32_t s_dns_generation = 0U;
static volatile bool s_route_reconnect_pending = false;
static volatile uint32_t s_route_generation = 0U;
static TickType_t s_route_reconnect_at = 0;
static uint32_t s_mqtt_connect_generation = 0U;
static uint32_t s_mqtt_attempt_epoch = 0U;
static uint32_t s_mqtt_active_attempt_epoch = 0U;
static bool s_mqtt_disconnect_expected = false;
static bool s_mqtt_disconnecting = false;

typedef struct {
    uint32_t id;
    uint32_t epoch;
    uint32_t generation;
    bool in_use;
    bool completed;
    TickType_t quarantine_until;
    char hostname[64];
    ip_addr_t address;
} mqtt_dns_request_t;
#define MQTT_DNS_REQUEST_POOL_SIZE 4U
static mqtt_dns_request_t s_dns_request_pool[MQTT_DNS_REQUEST_POOL_SIZE];
static uint32_t s_dns_request_id = 0U;
static mqtt_dns_request_t *s_dns_active_request = NULL;

typedef struct {
    ip_addr_t address;
    uint16_t port;
    char username[32];
    char password[32];
    char base_topic[64];
    char client_id[48];
    uint16_t keepalive_s;
    uint32_t override_generation;   /* SimplePLC port: generation this attempt was built from */
    uint32_t route_generation;
    uint32_t attempt_epoch;
    bool in_use;
    bool terminal;
    TickType_t quarantine_until;
} mqtt_connect_command_t;

#define MQTT_ATTEMPT_POOL_SIZE 4U
static mqtt_connect_command_t *s_mqtt_attempt_context = NULL;
static mqtt_connect_command_t s_mqtt_attempt_pool[MQTT_ATTEMPT_POOL_SIZE];

static mqtt_connect_command_t *mqtt_attempt_acquire(void)
{
    TickType_t now = xTaskGetTickCount();
    for (size_t i = 0U; i < MQTT_ATTEMPT_POOL_SIZE; ++i) {
        if (!s_mqtt_attempt_pool[i].in_use &&
            (int32_t)(now - s_mqtt_attempt_pool[i].quarantine_until) >= 0) {
            memset(&s_mqtt_attempt_pool[i], 0, sizeof(s_mqtt_attempt_pool[i]));
            s_mqtt_attempt_pool[i].in_use = true;
            return &s_mqtt_attempt_pool[i];
        }
    }
    return NULL;
}

static void mqtt_attempt_release(mqtt_connect_command_t *attempt)
{
    if (attempt == NULL) return;
    attempt->in_use = false;
    attempt->terminal = true;
    attempt->quarantine_until = xTaskGetTickCount() +
                                pdMS_TO_TICKS(MQTT_DNS_QUARANTINE_MS);
    if (s_mqtt_attempt_context == attempt) s_mqtt_attempt_context = NULL;
}

static void mqtt_flush_offline_cb(void *arg);
static const char *mqtt_default_route_name(void);

static mqtt_dns_request_t *mqtt_dns_request_acquire(void)
{
    /* lwIP DNS is asynchronous and cannot be cancelled.  Keep exactly one
     * request alive until its terminal callback has returned. */
    if (s_dns_active_request != NULL) return NULL;
    for (size_t i = 0U; i < MQTT_DNS_REQUEST_POOL_SIZE; ++i) {
        if (!s_dns_request_pool[i].in_use &&
            (int32_t)(xTaskGetTickCount() - s_dns_request_pool[i].quarantine_until) >= 0) {
            memset(&s_dns_request_pool[i], 0, sizeof(s_dns_request_pool[i]));
            s_dns_request_pool[i].in_use = true;
            s_dns_active_request = &s_dns_request_pool[i];
            return &s_dns_request_pool[i];
        }
    }
    return NULL;
}

static void mqtt_dns_request_release(mqtt_dns_request_t *request)
{
    if (request != NULL) {
        request->in_use = false;
        request->quarantine_until = xTaskGetTickCount() + pdMS_TO_TICKS(MQTT_DNS_QUARANTINE_MS);
        if (s_dns_active_request == request) s_dns_active_request = NULL;
    }
}

static void mqtt_dns_complete(const char *name, const ip_addr_t *ipaddr,
                              mqtt_dns_request_t *request) {
    uint32_t current_generation = network_get_route_generation();
    if (request == NULL || request->completed) return;
    request->completed = true;
    if (request->id != s_dns_request_id ||
        request->epoch != s_mqtt_active_attempt_epoch ||
        request->generation != current_generation) {
        if (s_dns_active_request == request) {
            s_dns_in_progress = false;
            s_dns_resolved = false;
        }
        s_mqtt_stale_callbacks++;
        LOGD(TAG, "DNS stale: id=%lu epoch=%lu current_epoch=%lu generation=%lu current=%lu",
             (unsigned long)request->id, (unsigned long)request->generation,
             (unsigned long)s_mqtt_active_attempt_epoch,
             (unsigned long)request->generation, (unsigned long)current_generation);
        mqtt_dns_request_release(request);
        if (s_mqtt_task != NULL) xTaskNotifyGive(s_mqtt_task);
        return;
    }
    if (ipaddr != NULL) {
        s_resolved_ip = *ipaddr;
        s_dns_resolved = true;
        LOGI(TAG, "DNS resolved: id=%lu ip=%s route=%s generation=%lu",
             (unsigned long)request->id, ipaddr_ntoa(ipaddr),
             mqtt_default_route_name(), (unsigned long)request->generation);
    } else {
        s_dns_resolved = false;
        s_mqtt_connection_failed = true;
        s_mqtt_dns_failures++;
        LOGW(TAG, "DNS failed: id=%lu host=%s", (unsigned long)request->id,
             name != NULL ? name : request->hostname);
    }
    s_dns_in_progress = false;
    /* DNS success is consumed by the MQTT task; keep the state terminal until
     * that task creates the single connect attempt. */
    mqtt_dns_request_release(request);
    if (s_mqtt_task != NULL) xTaskNotifyGive(s_mqtt_task);
}

static void dns_found_cb(const char *name, const ip_addr_t *ipaddr, void *callback_arg) {
    mqtt_dns_complete(name, ipaddr, (mqtt_dns_request_t *)callback_arg);
}


static void mqtt_connection_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status) {
    mqtt_connect_command_t *attempt = (mqtt_connect_command_t *)arg;
    if (attempt == NULL || attempt->attempt_epoch != s_mqtt_active_attempt_epoch ||
        attempt->route_generation != network_get_route_generation()) {
        s_mqtt_stale_callbacks++;
        LOGD(TAG, "MQTT stale callback: epoch=%lu current_epoch=%lu generation=%lu",
             attempt != NULL ? (unsigned long)attempt->attempt_epoch : 0UL,
             (unsigned long)s_mqtt_active_attempt_epoch,
             attempt != NULL ? (unsigned long)attempt->route_generation : 0UL);
        /* The task owns the attempt context lifetime until terminal cleanup. */
        return;
    }
    s_mqtt_last_status = status;
    s_mqtt_connecting = false;
    s_mqtt_connect_started_at = 0;
    s_mqtt_disconnecting = false;
    if (status == MQTT_CONNECT_ACCEPTED) {
        if (s_route_reconnect_pending || s_mqtt_disconnect_expected) {
            s_mqtt_connected = false;
            s_mqtt_disconnect_expected = true;
            mqtt_disconnect(client);
            s_mqtt_connection_failed = true;
            attempt->terminal = true;
            return;
        }
        LOGI(TAG, "MQTT Connected: route=%s generation=%lu",
             mqtt_default_route_name(),
             (unsigned long)s_mqtt_connect_generation);
        s_connected_override_gen = attempt->override_generation;
        s_mqtt_connected = true;
        s_mqtt_state = MQTT_STATE_CONNECTED;
        s_mqtt_connect_attempt = 0U;
        (void)tcpip_try_callback(mqtt_flush_offline_cb, NULL);
    } else {
        bool expected = s_mqtt_disconnect_expected;
        s_mqtt_disconnect_expected = false;
        s_mqtt_last_error = (int)status;
        LOGW(TAG, "MQTT connect failed: epoch=%lu status=%d",
             (unsigned long)attempt->attempt_epoch, status);
        s_mqtt_connected = false;
        s_mqtt_connection_failed = !expected;
        s_mqtt_state = expected ? MQTT_STATE_DISCONNECTING : MQTT_STATE_BACKOFF;
        attempt->terminal = true;
        s_dns_resolved = false;
        s_dns_in_progress = false;
        if (s_mqtt_task != NULL) xTaskNotifyGive(s_mqtt_task);
    }
    /* Keep the context until the MQTT task observes the terminal event. */
}

static void mqtt_publish_cb(void *arg, err_t err) {
    (void)arg;
    if (err != ERR_OK) {
        LOGE(TAG, "Publish failed with error %d", err);
    }
}

static void mqtt_flush_offline_cb(void *arg) {
    mqtt_offline_item_t item;
    (void)arg;
    while (s_mqtt_connected && s_mqtt_client != NULL && s_offline_mutex != NULL) {
        if (xSemaphoreTake(s_offline_mutex, 0U) != pdTRUE) return;
        if (s_offline_count == 0U) {
            xSemaphoreGive(s_offline_mutex);
            return;
        }
        item = s_offline_queue[0];
        err_t err = mqtt_publish(s_mqtt_client, item.topic, item.payload,
                                 strlen(item.payload), mqtt_config_qos(),
                                 mqtt_config_retain(),
                                 mqtt_publish_cb, NULL);
        if (err != ERR_OK) {
            xSemaphoreGive(s_offline_mutex);
            return;
        }
        for (uint32_t i = 1U; i < s_offline_count; ++i) {
            s_offline_queue[i - 1U] = s_offline_queue[i];
        }
        --s_offline_count;
        xSemaphoreGive(s_offline_mutex);
    }
}

bool app_mqtt_is_connected(void) {
    return s_mqtt_connected;
}

const char *app_mqtt_state_name(void)
{
    static const char *const names[] = {
        "IDLE", "DNS_PENDING", "CONNECTING", "CONNECTED",
        "DISCONNECTING", "BACKOFF"
    };
    return ((unsigned)s_mqtt_state < (sizeof(names) / sizeof(names[0]))) ?
           names[s_mqtt_state] : "UNKNOWN";
}

uint32_t app_mqtt_attempt_epoch(void) { return s_mqtt_active_attempt_epoch; }
uint32_t app_mqtt_dns_failures(void) { return s_mqtt_dns_failures; }
uint32_t app_mqtt_connect_timeouts(void) { return s_mqtt_connect_timeouts; }
uint32_t app_mqtt_stale_callbacks(void) { return s_mqtt_stale_callbacks; }
int app_mqtt_last_error(void) { return s_mqtt_last_error; }
extern volatile uint32_t uart_log_dropped_frames;
uint32_t app_mqtt_dropped_uart_frames(void) { return uart_log_dropped_frames; }
uint32_t app_mqtt_dropped_vendor_logs(void) { return wifi_driver_vendor_log_dropped(); }

static void mqtt_disconnect_cb(void *arg) {
    (void)arg;
    s_mqtt_connected = false;
    s_mqtt_connecting = false;
    s_mqtt_connect_started_at = 0;
    s_mqtt_reconnect_pending = false;
    s_mqtt_disconnecting = false;
    s_mqtt_disconnect_expected = false;
    s_mqtt_state = MQTT_STATE_IDLE;
    s_dns_resolved = false;
    s_dns_in_progress = false;
    if (s_mqtt_attempt_context != NULL) {
        s_mqtt_attempt_context->terminal = true;
    }
    if (s_mqtt_client != NULL) mqtt_disconnect(s_mqtt_client);
    if (s_mqtt_task != NULL) xTaskNotifyGive(s_mqtt_task);
}

extern bool g_mqtt_config_changed;
void app_mqtt_request_reconnect(void) {
    if (!g_mqtt_config_changed) {
        LOGI(TAG, "MQTT configuration unchanged, skipping apply");
        return;
    }
    g_mqtt_config_changed = false;

    s_mqtt_reconnect_requested = true;
    s_mqtt_reconnect_pending = true;
    if (s_mqtt_task != NULL) xTaskNotifyGive(s_mqtt_task);
}

static void mqtt_route_changed(net_if_type_t old_interface,
                               net_if_type_t new_interface,
                               uint32_t generation,
                               void *context) {
    (void)context;
    s_route_generation = generation;
    s_route_reconnect_pending = true;
    s_route_reconnect_at = xTaskGetTickCount() +
                           pdMS_TO_TICKS(MQTT_ROUTE_DEBOUNCE_MS);
    LOGI(TAG, "Route event: %s(%d) -> %s(%d) generation=%lu",
         network_interface_name(old_interface), old_interface,
         network_interface_name(new_interface), new_interface,
         (unsigned long)generation);
    if (s_mqtt_connected || s_mqtt_connecting || s_mqtt_disconnecting) {
        LOGI(TAG, "MQTT disconnect scheduled: reason=route_changed old_route=%s",
             network_interface_name(old_interface));
    }
    LOGI(TAG, "MQTT reconnect scheduled: route=%s generation=%lu",
         network_interface_name(new_interface), (unsigned long)generation);
    if (s_mqtt_task != NULL) {
        xTaskNotifyGive(s_mqtt_task);
    }
}

static bool is_default_route_ready(void) {
    /* Snapshot-only readiness check. Route mutation is serialized by the
     * network manager in the TCP/IP thread; never wait on the core mutex here. */
    struct netif *netif = netif_default;
    return netif != NULL && netif_is_up(netif) && netif_is_link_up(netif) &&
           !ip4_addr_isany_val(*netif_ip4_addr(netif));
}

static const char *mqtt_default_route_name(void) {
    return network_interface_name(network_get_active_interface());
}

static void mqtt_dns_request_cb(void *arg) {
    err_t err;
    mqtt_dns_request_t *request = (mqtt_dns_request_t *)arg;
    uint32_t generation = request != NULL ? request->generation : 0U;
    char dns_primary[IPADDR_STRLEN_MAX];
    char dns_secondary[IPADDR_STRLEN_MAX];

    if (request == NULL || request->id != s_dns_request_id ||
        request->epoch != s_mqtt_active_attempt_epoch ||
        generation != network_get_route_generation()) {
        if (s_dns_active_request == request) {
            s_dns_in_progress = false;
            s_dns_resolved = false;
        }
        mqtt_dns_request_release(request);
        if (s_mqtt_task != NULL) xTaskNotifyGive(s_mqtt_task);
        return;
    }

    if (ip_addr_isany(dns_getserver(0))) {
        ip_addr_t fallback;
        IP4_ADDR(&fallback, 8, 8, 8, 8);
        dns_setserver(0, &fallback);
        IP4_ADDR(&fallback, 1, 1, 1, 1);
        dns_setserver(1, &fallback);
    }
    ipaddr_ntoa_r(dns_getserver(0), dns_primary, sizeof(dns_primary));
    ipaddr_ntoa_r(dns_getserver(1), dns_secondary, sizeof(dns_secondary));
    LOGI(TAG, "DNS servers used: primary=%s secondary=%s route=%s",
         dns_primary, dns_secondary,
         mqtt_default_route_name());
    err = dns_gethostbyname(request->hostname, &request->address, dns_found_cb, request);
    if (err == ERR_OK) {
        mqtt_dns_complete(request->hostname, &request->address, request);
    } else if (err != ERR_INPROGRESS) {
        s_dns_in_progress = false;
        s_mqtt_connection_failed = true;
        s_mqtt_dns_failures++;
        s_mqtt_last_error = (int)err;
        request->completed = true;
        LOGE(TAG, "DNS failed: id=%lu host=%s error=%d",
             (unsigned long)request->id, request->hostname, err);
        s_mqtt_state = MQTT_STATE_BACKOFF;
        mqtt_dns_request_release(request);
        if (s_mqtt_task != NULL) xTaskNotifyGive(s_mqtt_task);
    }
}

static void mqtt_connect_request_cb(void *arg) {
    mqtt_connect_command_t *command = (mqtt_connect_command_t *)arg;
    struct mqtt_connect_client_info_t ci;
    char lwt_topic[128];
    err_t err;

    if (command == NULL || s_mqtt_client == NULL) {
        if (command != NULL) command->terminal = true;
        return;
    }
    if (command->route_generation != network_get_route_generation() ||
        command->attempt_epoch != s_mqtt_active_attempt_epoch) {
        s_mqtt_stale_callbacks++;
        LOGD(TAG, "MQTT stale connect: epoch=%lu current_epoch=%lu",
             (unsigned long)command->route_generation,
             (unsigned long)s_mqtt_active_attempt_epoch);
        s_mqtt_connecting = false;
        s_mqtt_connection_failed = true;
        command->terminal = true;
        if (s_mqtt_task != NULL) xTaskNotifyGive(s_mqtt_task);
        return;
    }
    memset(&ci, 0, sizeof(ci));
    ci.client_id = (command->client_id[0] != '\0') ? command->client_id
                                                   : "GatewayV2_Client";
    ci.client_user = (command->username[0] != '\0') ? command->username : NULL;
    ci.client_pass = (command->password[0] != '\0') ? command->password : NULL;
    /* The bundled lwIP client has no clean-session knob; it always requests
     * a clean session.  keepalive comes from the persisted configuration. */
    ci.keep_alive = (command->keepalive_s != 0U) ? command->keepalive_s : 60U;
    snprintf(lwt_topic, sizeof(lwt_topic), "%s/status", command->base_topic);
    ci.will_topic = lwt_topic;
    ci.will_msg = "offline";
    ci.will_qos = 1;
    ci.will_retain = 1;

    err = mqtt_client_connect(s_mqtt_client, &command->address, command->port,
                              mqtt_connection_cb, command, &ci);
    if (err != ERR_OK) {
        s_mqtt_last_error = (int)err;
        LOGE(TAG, "mqtt_client_connect failed: %d", err);
        s_mqtt_connecting = false;
        s_mqtt_connect_started_at = 0;
        s_mqtt_connection_failed = true;
        command->terminal = true;
        if (s_mqtt_task != NULL) xTaskNotifyGive(s_mqtt_task);
        command->terminal = true;
    }
}

static bool mqtt_disconnect_async(void) {
    if (s_mqtt_disconnecting) return true;
    s_mqtt_disconnecting = true;
    s_mqtt_disconnect_expected = true;
    s_mqtt_state = MQTT_STATE_DISCONNECTING;
    return tcpip_try_callback(mqtt_disconnect_cb, NULL) == ERR_OK;
}

typedef struct {
    uint16_t topic_len;
    char data[];
} mqtt_publish_request_t;

static void mqtt_publish_request_cb(void *arg) {
    mqtt_publish_request_t *request = (mqtt_publish_request_t *)arg;
    const char *topic;
    const char *payload;
    err_t err = ERR_IF;

    if (request == NULL) return;
    topic = request->data;
    payload = &request->data[request->topic_len + 1U];
    if (s_mqtt_client != NULL && s_mqtt_connected) {
        err = mqtt_publish(s_mqtt_client, topic, payload, strlen(payload),
                           mqtt_config_qos(), mqtt_config_retain(),
                           mqtt_publish_cb, NULL);
    }
    if (err != ERR_OK) LOGE(TAG, "Publish queue failed with error %d", err);
    vPortFree(request);
}

static void mqtt_reconnect_task(void *params) {
    (void)params;
    ip_addr_t broker_ip;
    uint32_t backoff_ms = MQTT_RECONNECT_NORMAL_MS;
    TickType_t next_attempt_at = 0;

    for (;;) {
        TickType_t now = xTaskGetTickCount();
        bool mqtt_enabled;
        sys_config_lock();
        mqtt_enabled = g_sys_config.mqtt.enabled;
        sys_config_unlock();
        if (s_override.active) mqtt_enabled = true;   /* App-configured broker (SimplePLC) */

        if (!mqtt_enabled && (s_mqtt_connected || s_mqtt_connecting ||
                              s_dns_in_progress)) {
            s_mqtt_reconnect_pending = false;
            s_mqtt_reconnect_requested = false;
            s_mqtt_state = MQTT_STATE_DISCONNECTING;
            if (!mqtt_disconnect_async()) s_mqtt_disconnecting = false;
        }

        if (s_route_reconnect_pending &&
            (int32_t)(now - s_route_reconnect_at) >= 0) {
            s_route_reconnect_pending = false;
            s_dns_resolved = false;

            /* A DNS request cannot be cancelled.  Invalidate its identity and
             * wait for its callback before starting the new route attempt. */
            if (s_dns_active_request != NULL) {
                s_mqtt_attempt_epoch++;
                s_mqtt_active_attempt_epoch = s_mqtt_attempt_epoch;
            } else if (s_mqtt_connected || s_mqtt_connecting ||
                       s_mqtt_disconnecting) {
                s_mqtt_reconnect_requested = true;
                s_mqtt_reconnect_pending = true;
            } else {
                s_mqtt_state = MQTT_STATE_IDLE;
                s_mqtt_reconnect_pending = false;
                next_attempt_at = now;
            }
        }

        if (s_mqtt_state == MQTT_STATE_BACKOFF &&
            (int32_t)(now - next_attempt_at) >= 0) {
            s_mqtt_state = MQTT_STATE_IDLE;
        }

        if (s_mqtt_state == MQTT_STATE_DNS_PENDING &&
            s_dns_active_request == NULL && s_dns_resolved) {
            s_mqtt_state = MQTT_STATE_IDLE;
        }

        /* The stale DNS callback is the terminal event for the invalidated
         * request.  Only then may the FSM leave DNS_PENDING. */
        if (s_mqtt_state == MQTT_STATE_DNS_PENDING &&
            s_dns_active_request == NULL && !s_dns_in_progress &&
            !s_dns_resolved) {
            s_mqtt_state = MQTT_STATE_IDLE;
            next_attempt_at = now;
        }

        if (s_mqtt_attempt_context != NULL &&
            s_mqtt_attempt_context->terminal && !s_mqtt_connecting &&
            !s_mqtt_disconnecting && !s_mqtt_connected) {
            mqtt_attempt_release(s_mqtt_attempt_context);
        }

        if (s_mqtt_connection_failed) {
            s_mqtt_connection_failed = false;
            if (is_default_route_ready()) {
                backoff_ms = (backoff_ms < MQTT_RECONNECT_MAX_MS / 2U) ?
                             backoff_ms * 2U : MQTT_RECONNECT_MAX_MS;
            } else {
                /* A DHCP/failover interruption is not a broker failure. */
                backoff_ms = MQTT_RECONNECT_MIN_MS;
            }
            next_attempt_at = now + pdMS_TO_TICKS(backoff_ms);
            s_mqtt_state = MQTT_STATE_BACKOFF;
        }

        if (s_mqtt_reconnect_requested) {
            s_mqtt_reconnect_requested = false;
            s_mqtt_connection_failed = false;
            if (!mqtt_disconnect_async()) {
                s_mqtt_disconnecting = false;
                s_mqtt_reconnect_requested = true;
            }
            backoff_ms = MQTT_RECONNECT_MIN_MS;
            next_attempt_at = xTaskGetTickCount() + pdMS_TO_TICKS(backoff_ms);
            continue;
        }

        if (s_mqtt_connecting && s_mqtt_connect_started_at != 0 &&
            (xTaskGetTickCount() - s_mqtt_connect_started_at) >=
                pdMS_TO_TICKS(MQTT_CONNECT_TIMEOUT_MS)) {
            s_mqtt_connect_timeouts++;
            s_mqtt_last_error = MQTT_CONNECT_TIMEOUT;
            s_mqtt_state = MQTT_STATE_DISCONNECTING;
            LOGW(TAG, "MQTT timeout: epoch=%lu route=%s generation=%lu",
                 (unsigned long)s_mqtt_active_attempt_epoch,
                 mqtt_default_route_name(),
                 (unsigned long)network_get_route_generation());
            s_mqtt_reconnect_pending = true;
            if (!mqtt_disconnect_async()) {
                s_mqtt_reconnect_requested = true;
            }
            backoff_ms = (backoff_ms < MQTT_RECONNECT_MAX_MS / 2U) ?
                         backoff_ms * 2U : MQTT_RECONNECT_MAX_MS;
            s_mqtt_state = MQTT_STATE_BACKOFF;
            LOGI(TAG, "MQTT retry scheduled: delay=%lu ms attempt=%lu",
                 (unsigned long)backoff_ms,
                 (unsigned long)s_mqtt_connect_attempt);
            next_attempt_at = xTaskGetTickCount() + pdMS_TO_TICKS(backoff_ms);
            continue;
        }

        if (s_mqtt_state == MQTT_STATE_IDLE &&
            !s_mqtt_reconnect_pending && !s_mqtt_connected &&
            !s_mqtt_connecting && !s_mqtt_disconnecting && s_mqtt_client &&
            !s_route_reconnect_pending &&
            is_default_route_ready() &&
            !s_mqtt_disconnecting &&
            (int32_t)(now - next_attempt_at) >= 0) {
            char broker_url[64];
            char username[32];
            char password[32];
            char base_topic[64];
            char client_id[48];
            uint16_t keepalive_s;
            uint16_t port;
            bool enabled;
            
            sys_config_lock();
            enabled = g_sys_config.mqtt.enabled;
            strncpy(broker_url, g_sys_config.mqtt.broker_url, sizeof(broker_url) - 1U);
            broker_url[sizeof(broker_url) - 1U] = '\0';
            strncpy(username, g_sys_config.mqtt.username, sizeof(username) - 1U);
            username[sizeof(username) - 1U] = '\0';
            strncpy(password, g_sys_config.mqtt.password, sizeof(password) - 1U);
            password[sizeof(password) - 1U] = '\0';
            strncpy(base_topic,
                    g_sys_config.mqtt.publish_topic[0] != '\0'
                      ? g_sys_config.mqtt.publish_topic
                      : g_sys_config.mqtt.base_topic,
                    sizeof(base_topic) - 1U);
            base_topic[sizeof(base_topic) - 1U] = '\0';
            strncpy(client_id, g_sys_config.mqtt.client_id, sizeof(client_id) - 1U);
            client_id[sizeof(client_id) - 1U] = '\0';
            keepalive_s = g_sys_config.mqtt.keepalive_s;
            port = g_sys_config.mqtt.port;
            sys_config_unlock();

            /* SimplePLC port: broker configured by the App overrides the host's
             * broker/credentials/client id/keepalive. base_topic (LWT) stays the host's. */
            uint32_t override_generation;
            {
                mqtt_config_override_t ov;
                mqtt_override_get(&ov);
                override_generation = ov.generation;
                if (ov.active) {
                    enabled = true;
                    strncpy(broker_url, ov.broker_url, sizeof(broker_url) - 1U);
                    broker_url[sizeof(broker_url) - 1U] = '\0';
                    strncpy(username, ov.username, sizeof(username) - 1U);
                    username[sizeof(username) - 1U] = '\0';
                    strncpy(password, ov.password, sizeof(password) - 1U);
                    password[sizeof(password) - 1U] = '\0';
                    strncpy(client_id, ov.client_id, sizeof(client_id) - 1U);
                    client_id[sizeof(client_id) - 1U] = '\0';
                    keepalive_s = ov.keepalive_s;
                    port = ov.port;
                }
            }

            if (enabled && strlen(broker_url) > 0) {
                if (ipaddr_aton(broker_url, &broker_ip)) {
                    s_resolved_ip = broker_ip;
                    s_dns_resolved = true;
                } else if (s_mqtt_state == MQTT_STATE_IDLE &&
                           !s_dns_in_progress && !s_dns_resolved &&
                           s_dns_active_request == NULL &&
                           s_mqtt_attempt_context == NULL) {
                    mqtt_dns_request_t *request = mqtt_dns_request_acquire();
                    if (request == NULL) {
                        s_mqtt_connection_failed = true;
                        LOGE(TAG, "DNS request allocation failed");
                        next_attempt_at = xTaskGetTickCount() +
                                          pdMS_TO_TICKS(backoff_ms);
                        continue;
                    }
                    s_dns_in_progress = true;
                    s_dns_generation = network_get_route_generation();
                    s_dns_request_id++;
                    request->id = s_dns_request_id;
                    /* One epoch covers DNS + connect for one attempt. */
                    s_mqtt_attempt_epoch++;
                    s_mqtt_active_attempt_epoch = s_mqtt_attempt_epoch;
                    request->epoch = s_mqtt_active_attempt_epoch;
                    request->generation = s_dns_generation;
                    strncpy(request->hostname, broker_url, sizeof(request->hostname) - 1U);
                    request->hostname[sizeof(request->hostname) - 1U] = '\0';
                    s_mqtt_state = MQTT_STATE_DNS_PENDING;
                    LOGI(TAG, "DNS request start: id=%lu host=%s epoch=%lu route=%s generation=%lu",
                         (unsigned long)request->id, request->hostname,
                         (unsigned long)request->epoch, mqtt_default_route_name(),
                         (unsigned long)s_dns_generation);
                    if (tcpip_try_callback(mqtt_dns_request_cb,
                                           request) != ERR_OK) {
                        s_dns_in_progress = false;
                        mqtt_dns_request_release(request);
                        s_mqtt_connection_failed = true;
                        LOGE(TAG, "Failed to queue DNS request");
                        next_attempt_at = xTaskGetTickCount() +
                                          pdMS_TO_TICKS(backoff_ms);
                    }
                }

                if (s_dns_resolved && s_mqtt_state == MQTT_STATE_IDLE) {
                    mqtt_connect_command_t *command = mqtt_attempt_acquire();
                    if (command == NULL) {
                        s_mqtt_connection_failed = true;
                        LOGE(TAG, "MQTT attempt allocation failed");
                        continue;
                    }
                    command->address = s_resolved_ip;
                    command->port = port;
                    strncpy(command->username, username, sizeof(command->username) - 1U);
                    strncpy(command->password, password, sizeof(command->password) - 1U);
                    strncpy(command->base_topic, base_topic, sizeof(command->base_topic) - 1U);
                    strncpy(command->client_id, client_id, sizeof(command->client_id) - 1U);
                    command->keepalive_s = keepalive_s;
                    command->override_generation = override_generation;
                    command->route_generation = network_get_route_generation();
                    if (s_mqtt_attempt_context != NULL ||
                        s_mqtt_state != MQTT_STATE_IDLE) {
                        mqtt_attempt_release(command);
                        continue;
                    }
                    command->attempt_epoch = s_mqtt_active_attempt_epoch;
                    s_mqtt_attempt_context = command;
                    s_mqtt_connect_generation = command->route_generation;
                    s_mqtt_connecting = true;
                    s_mqtt_state = MQTT_STATE_CONNECTING;
                    s_mqtt_connect_attempt++;
                    s_mqtt_connect_started_at = xTaskGetTickCount();
                    s_mqtt_last_status = MQTT_CONNECT_TIMEOUT;
                    s_dns_resolved = false;
                    LOGI(TAG, "MQTT connect start: epoch=%lu broker=%s:%d ip=%s route=%s generation=%lu",
                         (unsigned long)command->attempt_epoch,
                         broker_url, port, ipaddr_ntoa(&command->address),
                         mqtt_default_route_name(),
                         (unsigned long)command->route_generation);
                    if (tcpip_try_callback(mqtt_connect_request_cb, command) != ERR_OK) {
                        s_mqtt_connecting = false;
                        s_mqtt_connect_started_at = 0;
                        mqtt_attempt_release(command);
                        s_mqtt_connection_failed = true;
                        backoff_ms = (backoff_ms < MQTT_RECONNECT_MAX_MS / 2U) ?
                                     backoff_ms * 2U : MQTT_RECONNECT_MAX_MS;
                        next_attempt_at = xTaskGetTickCount() +
                                          pdMS_TO_TICKS(backoff_ms);
                    }
                }
            } else if (!enabled) {
                s_mqtt_state = MQTT_STATE_IDLE;
                next_attempt_at = now + pdMS_TO_TICKS(MQTT_RECONNECT_MAX_MS);
            }
        }
        
        if (s_mqtt_connected) {
            backoff_ms = MQTT_RECONNECT_NORMAL_MS;
            next_attempt_at = 0;
        }

        /* Poll route readiness at 1 s so DHCP recovery does not inherit a
         * long broker backoff. The attempt deadline still rate-limits actual
         * DNS/connect operations. */
        uint32_t wait_ms = MQTT_RECONNECT_MIN_MS;
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait_ms));
    }
}

void mqtt_client_init(void) {
    LOGI(TAG, "MQTT Client Init");
    s_mqtt_client = mqtt_client_new();
    if (s_mqtt_client == NULL) {
        LOGE(TAG, "Failed to create MQTT client");
        return;
    }
    s_offline_mutex = xSemaphoreCreateMutex();
    if (s_offline_mutex == NULL) {
        LOGE(TAG, "Failed to create MQTT offline queue mutex");
    }
    s_route_generation = network_get_route_generation();
    (void)network_manager_register_route_callback(mqtt_route_changed, NULL);
    xTaskCreate(mqtt_reconnect_task, "MQTT_RECONN", 2048, NULL, 10, &s_mqtt_task);
}

int mqtt_client_publish(const char *topic, const char *payload) {
    mqtt_publish_request_t *request;
    size_t topic_len;
    size_t payload_len;
    size_t request_size;

    if (topic == NULL || payload == NULL || !s_mqtt_client) {
        return -1;
    }

    if (!s_mqtt_connected) {
        mqtt_offline_item_t item;
        if (s_offline_mutex == NULL ||
            xSemaphoreTake(s_offline_mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
            return -1;
        }
        if (s_offline_count >= MQTT_OFFLINE_QUEUE_SIZE) {
            memmove(&s_offline_queue[0], &s_offline_queue[1],
                    (MQTT_OFFLINE_QUEUE_SIZE - 1U) * sizeof(item));
            s_offline_count = MQTT_OFFLINE_QUEUE_SIZE - 1U;
            s_offline_dropped++;
        }
        strncpy(item.topic, topic, sizeof(item.topic) - 1U);
        item.topic[sizeof(item.topic) - 1U] = '\0';
        strncpy(item.payload, payload, sizeof(item.payload) - 1U);
        item.payload[sizeof(item.payload) - 1U] = '\0';
        s_offline_queue[s_offline_count++] = item;
        xSemaphoreGive(s_offline_mutex);
        return 0;
    }

    topic_len = strlen(topic);
    payload_len = strlen(payload);
    if (topic_len > UINT16_MAX ||
        topic_len > SIZE_MAX - payload_len - sizeof(*request) - 2U) {
        return -1;
    }

    request_size = sizeof(*request) + topic_len + 1U + payload_len + 1U;
    request = pvPortMalloc(request_size);
    if (request == NULL) return -1;

    request->topic_len = (uint16_t)topic_len;
    memcpy(request->data, topic, topic_len + 1U);
    memcpy(&request->data[topic_len + 1U], payload, payload_len + 1U);

    if (tcpip_try_callback(mqtt_publish_request_cb, request) != ERR_OK) {
        vPortFree(request);
        return -1;
    }
    return 0;
}

/* ---- SimplePLC port: publish with its own QoS / retain ---------------------
 * Same path as mqtt_client_publish() (copy into one heap block, run on the
 * tcpip thread) but QoS/retain come from the caller instead of g_sys_config,
 * and nothing is queued while disconnected: returns -1 so the caller retries. */
typedef struct {
    uint16_t topic_len;
    uint8_t qos;
    uint8_t retain;
    char data[];
} mqtt_publish_ex_request_t;

static void mqtt_publish_ex_request_cb(void *arg) {
    mqtt_publish_ex_request_t *request = (mqtt_publish_ex_request_t *)arg;
    const char *topic;
    const char *payload;
    err_t err = ERR_IF;

    if (request == NULL) return;
    topic = request->data;
    payload = &request->data[request->topic_len + 1U];
    if (s_mqtt_client != NULL && s_mqtt_connected) {
        err = mqtt_publish(s_mqtt_client, topic, payload, strlen(payload),
                           request->qos, request->retain != 0U,
                           mqtt_publish_cb, NULL);
    }
    if (err != ERR_OK) LOGE(TAG, "Publish(ex) queue failed with error %d", err);
    vPortFree(request);
}

int mqtt_client_publish_ex(const char *topic, const char *payload,
                           uint8_t qos, bool retain) {
    mqtt_publish_ex_request_t *request;
    size_t topic_len;
    size_t payload_len;
    size_t request_size;

    if (topic == NULL || payload == NULL || !s_mqtt_client || !s_mqtt_connected) {
        return -1;
    }
    topic_len = strlen(topic);
    payload_len = strlen(payload);
    if (topic_len > UINT16_MAX ||
        topic_len > SIZE_MAX - payload_len - sizeof(*request) - 2U) {
        return -1;
    }
    request_size = sizeof(*request) + topic_len + 1U + payload_len + 1U;
    request = pvPortMalloc(request_size);
    if (request == NULL) return -1;

    request->topic_len = (uint16_t)topic_len;
    request->qos = (qos <= 2U) ? qos : 1U;
    request->retain = retain ? 1U : 0U;
    memcpy(request->data, topic, topic_len + 1U);
    memcpy(&request->data[topic_len + 1U], payload, payload_len + 1U);

    if (tcpip_try_callback(mqtt_publish_ex_request_cb, request) != ERR_OK) {
        vPortFree(request);
        return -1;
    }
    return 0;
}