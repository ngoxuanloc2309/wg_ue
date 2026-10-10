/**
 * @file config_manager.h
 * @brief Configuration Management
 */
#ifndef __CONFIG_MANAGER_H__
#define __CONFIG_MANAGER_H__

#include <stdint.h>
#include <stdbool.h>
#include <cJSON.h>

/** Core System Config (Managed directly by config_manager) */
typedef struct {
    char path[64];
    char source[64];
    char type[12];
} mqtt_payload_field_t;

#define MQTT_PAYLOAD_MAX_FIELDS 16U

typedef struct {
    char device_name[32];
    char device_id[32];
} device_config_t;

typedef struct {
    bool enabled;
    char broker_url[64];
    uint16_t port;
    char base_topic[64];
    char username[32];
    char password[32];
    uint8_t qos;
    char client_id[48];
    uint16_t keepalive_s;
    bool clean_session;
    char publish_strategy[16];
    char publish_topic[96];
    uint32_t publish_interval_ms;
    bool publish_retain;
    char payload_mode[16];
    char payload_template[512];
    mqtt_payload_field_t payload_fields[MQTT_PAYLOAD_MAX_FIELDS];
    uint8_t payload_field_count;
} mqtt_config_t;

typedef struct {
    char admin_password[32];
    char user_password[32];
} security_config_t;

typedef struct {
    char server[64];
    int timezone;
} ntp_config_t;

typedef struct {
    device_config_t device;
    mqtt_config_t mqtt;
    security_config_t security;
    ntp_config_t ntp;
} sys_config_t;

typedef enum {
    CONFIG_RESULT_OK = 0,
    CONFIG_RESULT_INVALID = -1,
    CONFIG_RESULT_CONFLICT = -2,
    CONFIG_RESULT_UNSUPPORTED = -3,
    CONFIG_RESULT_STORAGE = -4
} config_result_t;

extern sys_config_t g_sys_config;

/* Callback types for distributed JSON parsing */
typedef void (*config_load_cb_t)(cJSON *node);
typedef void (*config_save_cb_t)(cJSON *root, bool include_secrets);
typedef void (*config_notify_cb_t)(void);
typedef void (*config_commit_cb_t)(void);
typedef int (*config_apply_cb_t)(void);

#define MAX_CONFIG_NODES 10

/* Register a module to handle its own JSON branch */
int config_manager_get_node_count(void);
typedef struct { const char *name; config_load_cb_t load; config_save_cb_t save; config_notify_cb_t notify; config_commit_cb_t commit; config_apply_cb_t apply; } config_node_t;
config_node_t *config_manager_get_node(int index);
void config_manager_register_node(const char *node_name, config_load_cb_t load_cb, config_save_cb_t save_cb, config_notify_cb_t notify_cb, config_commit_cb_t commit_cb, config_apply_cb_t apply_cb);

int config_load(void);
int config_save(void);
int config_update_from_json(const char *json_str);
int config_update_from_json_n(const char *json, size_t length);
char* config_get_json_string(void);
uint32_t config_get_generation(void);
const char *config_get_contract_version(void);
const char *config_get_last_error_path(void);
const char *config_get_last_error_code(void);
const char *config_get_last_error_message(void);

void sys_config_lock(void);
void sys_config_unlock(void);
sys_config_t* config_get_staging(void);

void config_manager_notify_all(void);
void config_manager_commit_all(void);

#endif /* __CONFIG_MANAGER_H__ */
