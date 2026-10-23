/**
 * @file splc_mqtt_port.c
 * @brief See splc_mqtt_port.h. Design: libs/simple_plc/docs/plc_mqtt_design.md.
 *
 * What the host client can and cannot do (verified against apps/mqtt_client):
 *  - QoS/retain per block: done through mqtt_client_publish_ex().
 *  - Broker host/port/user/password/client_id/keepalive from the App: applied
 *    through mqtt_client_set_override() at the next connect. LWT (base_topic),
 *    DNS, backoff and the offline queue are the host's and unchanged.
 *  - NOT possible with the bundled lwIP client: clean_session=0, MQTT 5, TLS.
 *    Those App fields are ignored here (TLS is already refused at COMMIT).
 *  - tx_message_count in the library counts publishes the host ACCEPTED, not
 *    broker acks (mqtt_publish() errors on the tcpip thread are only logged).
 */
#include "splc_mqtt_port.h"

#if SPLC_MQTT_ENABLE

#include <stdbool.h>
#include <string.h>

#include "plc_mqtt.h"
#if SPLC_MQTT_SUB_ENABLE
#include "plc_mqtt_sub.h"
#endif
#include "mqtt_client.h"
#include "config_manager.h"   /* g_sys_config, sys_config_lock() */
#include "splc_log.h"

#define TAG "SPLC_MQTT"

#define PUMP_PERIOD_MS       100U
#define RECONNECT_GUARD_MS  5000U   /* min gap between two reconnect requests */

/* Owned by apps/settings/config_manager.c; app_mqtt_request_reconnect() returns
 * early unless it is true (it is the "mqtt config really changed" flag). */
extern bool g_mqtt_config_changed;

static bool     s_have_pump;
static uint32_t s_last_pump_ms;
static bool     s_pushed;
static uint32_t s_pushed_gen;
static bool     s_have_reconnect;
static uint32_t s_last_reconnect_ms;
static bool     s_warned_gen_valid;
static uint32_t s_warned_gen;
#if SPLC_MQTT_SUB_ENABLE
static bool     s_sub_pushed;
static uint32_t s_sub_pushed_gen;
#endif

/* ---- publish callback ---------------------------------------------------- */

static int port_publish(const char *topic, const char *payload, size_t len,
                        uint8_t qos, bool retain, void *ctx)
{
    (void)len;
    (void)ctx;
    if (!app_mqtt_is_connected()) return -1;
    return (mqtt_client_publish_ex(topic, payload, qos, retain) == 0) ? 0 : -1;
}

/* ---- helpers ------------------------------------------------------------- */

/* The host buffers are smaller than the wire fields by one (31/31/47/63
 * usable characters). Never truncate a password silently: an App broker that
 * does not fit is NOT applied (the host's own broker stays in use). */
static bool fits_host(const splc_mqtt_broker_t *b)
{
    return strlen(b->host) < sizeof(((mqtt_config_override_t *)0)->broker_url) &&
           strlen(b->username) < sizeof(((mqtt_config_override_t *)0)->username) &&
           strlen(b->password) < sizeof(((mqtt_config_override_t *)0)->password) &&
           strlen(b->client_id) < sizeof(((mqtt_config_override_t *)0)->client_id);
}

static uint16_t map_last_error(bool connected)
{
    if (connected) return SPLC_MQTT_ERR_NONE;
    /* lwIP mqtt_connection_status_t: 4/5 = bad credentials / not authorized,
     * 256 = disconnected, 257 = timeout. Best effort, NOT checked on a board. */
    switch (app_mqtt_last_error()) {
    case 4:
    case 5:   return SPLC_MQTT_ERR_AUTH_FAIL;
    case 256: return SPLC_MQTT_ERR_TCP_RESET;
    case 257: return SPLC_MQTT_ERR_TIMEOUT;
    default:  return SPLC_MQTT_ERR_NONE;
    }
}

#if SPLC_MQTT_SUB_ENABLE
/* ---- MQTT Subscribe -------------------------------------------------------- */

/* Library -> host: the topics the App configured (re-read when splc_mqtt_sub_generation()
 * changes: COMMIT, Flash load, clear). The client subscribes them after every connect. */
static void sub_push_topics(void)
{
    mqtt_client_sub_t subs[MQTT_CLIENT_SUB_MAX];
    memset(subs, 0, sizeof(subs));
    for (uint8_t i = 0U; i < PLC_MQTT_SUB_BLOCKS && i < MQTT_CLIENT_SUB_MAX; i++) {
        char topic[PLC_MQTT_SUB_TOPIC_MAX + 1U];
        uint8_t qos = 0U;
        if (splc_mqtt_sub_get(i, topic, &qos)) {
            subs[i].used = true;
            subs[i].qos = (qos <= 1U) ? qos : 1U;     /* lwIP client: QoS 2 is not supported for SUB here */
            strncpy(subs[i].topic, topic, sizeof(subs[i].topic) - 1U);
            if (qos > 1U) {
                log_warn(TAG, "SUB block %u asks QoS 2: subscribed with QoS 1", (unsigned)i + 1U);
            }
        }
    }
    mqtt_client_set_subscriptions(subs);
}

/* Host -> library: hand over every message the client queued (engine task only). */
static void sub_drain_messages(void)
{
    mqtt_client_rx_t m;
    /* A few per call: the queue is small and the library applies one message per call anyway. */
    for (unsigned n = 0U; n < 4U && mqtt_client_sub_poll(&m); n++) {
        (void)splc_mqtt_input(m.topic, (const char *)m.payload, m.len, false);
    }
}
#endif /* SPLC_MQTT_SUB_ENABLE */

/* ---- public -------------------------------------------------------------- */

void splc_mqtt_port_init(void)
{
    char name[sizeof(g_sys_config.device.device_id)];

    if (!splc_mqtt_register(port_publish, NULL)) {
        log_error(TAG, "publish callback registration failed");
    }

    /* Read once, here: sys_config_lock() can be held for a long time by a
     * config save, and blocking the engine task in the scan loop is not OK. */
    sys_config_lock();
    strncpy(name, g_sys_config.device.device_id, sizeof(name) - 1U);
    sys_config_unlock();
    name[sizeof(name) - 1U] = '\0';
    splc_mqtt_set_default_device_name(name[0] != '\0' ? name : NULL);
}

void splc_mqtt_port_poll(uint32_t now_ms)
{
#if SPLC_MQTT_SUB_ENABLE
    sub_drain_messages();                /* cheap: one queue peek when nothing arrived */
#endif
    if (s_have_pump && (uint32_t)(now_ms - s_last_pump_ms) < PUMP_PERIOD_MS) return;
    s_have_pump = true;
    s_last_pump_ms = now_ms;

    /* 1. Broker config library -> host client (only when it changed). */
    splc_mqtt_broker_t b;
    bool configured = false;
    uint32_t gen = 0U;
    splc_mqtt_get_broker(&b, &configured, &gen);

    if (!s_pushed || gen != s_pushed_gen) {
        mqtt_config_override_t ov;
        memset(&ov, 0, sizeof(ov));
        ov.generation = gen;
        if (configured && !b.use_tls && fits_host(&b)) {
            ov.active = true;
            strncpy(ov.broker_url, b.host, sizeof(ov.broker_url) - 1U);
            strncpy(ov.username, b.username, sizeof(ov.username) - 1U);
            strncpy(ov.password, b.password, sizeof(ov.password) - 1U);
            strncpy(ov.client_id, b.client_id, sizeof(ov.client_id) - 1U);
            ov.port = b.port;
            ov.keepalive_s = b.keepalive_sec;
            if (b.clean_session == false || b.version != 0U) {
                log_warn(TAG, "App broker asks clean_session=0 or MQTT 5: not supported by the host client, ignored");
            }
        } else if (configured && (!s_warned_gen_valid || s_warned_gen != gen)) {
            s_warned_gen_valid = true;
            s_warned_gen = gen;
            log_warn(TAG, "App broker not applied (TLS or field longer than the host buffer): host broker stays in use");
        }
        mqtt_client_set_override(&ov);
        s_pushed = true;
        s_pushed_gen = gen;
    }

#if SPLC_MQTT_SUB_ENABLE
    const uint32_t sub_gen = splc_mqtt_sub_generation();
    if (!s_sub_pushed || sub_gen != s_sub_pushed_gen) {
        sub_push_topics();
        s_sub_pushed = true;
        s_sub_pushed_gen = sub_gen;
    }
#endif

    /* 2. Status host -> library. generation = the config the LIVE connection was
     * built from, so the library publishes only when it matches its own. */
    const bool connected = app_mqtt_is_connected();
    const uint32_t conn_gen = mqtt_client_connected_override_generation();
    splc_mqtt_status_t st;
    memset(&st, 0, sizeof(st));
    if (connected) st.flags = SPLC_MQTT_STATUS_CONNECTED | SPLC_MQTT_STATUS_AUTH_OK;
    st.last_error = map_last_error(connected);
    st.generation = conn_gen;
    splc_mqtt_set_status(&st);

    /* 3. A live connection built from an older config (the App changed the
     * broker, or this task started after the first connect): rebuild it. One
     * condition covers all cases; the guard stops a reconnect storm. */
    if (connected && conn_gen != gen &&
        (!s_have_reconnect || (uint32_t)(now_ms - s_last_reconnect_ms) >= RECONNECT_GUARD_MS)) {
        s_have_reconnect = true;
        s_last_reconnect_ms = now_ms;
        log_info(TAG, "broker config generation %lu != connection %lu: reconnecting",
                 (unsigned long)gen, (unsigned long)conn_gen);
        g_mqtt_config_changed = true;
        app_mqtt_request_reconnect();
    }
}

#endif /* SPLC_MQTT_ENABLE */