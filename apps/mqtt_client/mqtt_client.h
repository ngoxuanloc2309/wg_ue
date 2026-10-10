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

#endif /* __MQTT_CLIENT_H__ */
