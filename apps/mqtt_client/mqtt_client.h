/**
 * @file mqtt_client.h
 * @brief Generic MQTT Client wrapper over LwIP
 */
#ifndef __MQTT_CLIENT_H__
#define __MQTT_CLIENT_H__

#include <stdint.h>
#include <stdbool.h>

void mqtt_client_init(void);
int mqtt_client_publish(const char *topic, const char *payload);
bool app_mqtt_is_connected(void);
void app_mqtt_request_reconnect(void);
const char *app_mqtt_state_name(void);
uint32_t app_mqtt_attempt_epoch(void);
uint32_t app_mqtt_dns_failures(void);
uint32_t app_mqtt_connect_timeouts(void);
uint32_t app_mqtt_stale_callbacks(void);
int app_mqtt_last_error(void);
uint32_t app_mqtt_dropped_uart_frames(void);
uint32_t app_mqtt_dropped_vendor_logs(void);

/* ---- SimplePLC port hooks (ports/splc_port/splc_mqtt_port.c) ----------------
 * Broker configured by the App (0x0E00) replaces the host's broker, credentials,
 * client id and keepalive at the next connect. LWT/base_topic, DNS, backoff and
 * the offline queue are untouched. The lwIP client has no clean-session or
 * MQTT 5 knob, so those App fields are ignored. */
typedef struct {
    bool active;              /* true: use the fields below instead of g_sys_config.mqtt */
    char broker_url[64];
    uint16_t port;
    char username[32];
    char password[32];
    char client_id[48];
    uint16_t keepalive_s;
    uint32_t generation;      /* opaque tag, echoed by ..._connected_override_generation() */
} mqtt_config_override_t;

/* Safe from any task. Takes effect at the next connect attempt. */
void mqtt_client_set_override(const mqtt_config_override_t *ov);
/* Generation tag of the override the live connection was built from. */
uint32_t mqtt_client_connected_override_generation(void);
/* Like mqtt_client_publish() but with the caller's QoS/retain; returns -1 when
 * disconnected (nothing is queued), 0 = handed to the tcpip thread. */
int mqtt_client_publish_ex(const char *topic, const char *payload,
                           uint8_t qos, bool retain);


/* ---- SimplePLC port hooks: MQTT Subscribe (ports/splc_port/splc_mqtt_port.c) -------
 * The App's subscription blocks (0x0D80) name up to MQTT_CLIENT_SUB_MAX topics. The client
 * (re)subscribes them after every connect and whenever the set changes; incoming messages are
 * copied into a small queue (lwIP calls the callbacks on the tcpip thread) that the SimplePLC
 * engine task drains with mqtt_client_sub_poll(). lwIP does not report the RETAIN flag. */
#define MQTT_CLIENT_SUB_MAX            2U
#define MQTT_CLIENT_SUB_TOPIC_LEN      33U     /* 32 characters + NUL (PLC_MQTT_SUB_TOPIC_MAX) */
#define MQTT_CLIENT_RX_TOPIC_LEN       96U
#define MQTT_CLIENT_RX_PAYLOAD_LEN     128U    /* the library keeps at most 128 B per message */

typedef struct {
    bool    used;
    uint8_t qos;
    char    topic[MQTT_CLIENT_SUB_TOPIC_LEN];
} mqtt_client_sub_t;

typedef struct {
    char     topic[MQTT_CLIENT_RX_TOPIC_LEN];
    uint8_t  payload[MQTT_CLIENT_RX_PAYLOAD_LEN];
    uint16_t len;
} mqtt_client_rx_t;

/* Safe from any task. Replaces the wanted set; applied at once when connected. */
void mqtt_client_set_subscriptions(const mqtt_client_sub_t subs[MQTT_CLIENT_SUB_MAX]);
/* Copy the oldest queued message into *out. false = none. Never blocks. */
bool mqtt_client_sub_poll(mqtt_client_rx_t *out);
/* Messages dropped (queue full, or longer than MQTT_CLIENT_RX_PAYLOAD_LEN). */
uint32_t mqtt_client_sub_dropped(void);

#endif /* __MQTT_CLIENT_H__ */