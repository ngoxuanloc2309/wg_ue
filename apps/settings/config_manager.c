#include "config_manager.h"
#include "logger.h"
#include "file_io.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include "FreeRTOS.h"
#include "semphr.h"

#define TAG "CONFIG_MGR"
#define CONFIG_DIR "configs"
#define CONFIG_FILE CONFIG_DIR "/system.json"
#define CONFIG_FILE_TMP CONFIG_DIR "/system.json.tmp"
#define LEGACY_CONFIG_FILE "config/system.json"
#define CONFIG_SCHEMA_VERSION 1U
#define CONFIG_CONTRACT_VERSION "1.2"
#define CONFIG_MAX_JSON_SIZE (16U * 1024U)

sys_config_t g_sys_config;
static sys_config_t s_staging_config;
static SemaphoreHandle_t s_config_mutex = NULL;
static SemaphoreHandle_t s_config_transaction_mutex = NULL;
static uint32_t s_config_generation = 1U;
bool g_mqtt_config_changed = false;
static char s_last_error_path[96];
static char s_last_error_code[32];
static char s_last_error_message[128];

static void config_set_error(const char *path, const char *code, const char *message)
{
    snprintf(s_last_error_path, sizeof(s_last_error_path), "%s", path != NULL ? path : "");
    snprintf(s_last_error_code, sizeof(s_last_error_code), "%s", code != NULL ? code : "invalid");
    snprintf(s_last_error_message, sizeof(s_last_error_message), "%s", message != NULL ? message : "Invalid configuration");
}

static void config_clear_error(void)
{
    config_set_error("", "", "");
}

/* Registry for distributed config modules */
static config_node_t s_config_nodes[MAX_CONFIG_NODES];
static uint8_t s_node_count = 0;

void config_manager_register_node(const char *node_name, config_load_cb_t load_cb, config_save_cb_t save_cb, config_notify_cb_t notify_cb, config_commit_cb_t commit_cb, config_apply_cb_t apply_cb) {
    if (s_node_count < MAX_CONFIG_NODES) {
        s_config_nodes[s_node_count].name = node_name;
        s_config_nodes[s_node_count].load = load_cb;
        s_config_nodes[s_node_count].save = save_cb;
        s_config_nodes[s_node_count].notify = notify_cb;
        s_config_nodes[s_node_count].commit = commit_cb;
        s_config_nodes[s_node_count].apply = apply_cb;
        s_node_count++;
    }
}

void config_set_default(void) {
    strcpy(g_sys_config.device.device_name, "GatewayV2");
    strcpy(g_sys_config.device.device_id, "GW_000000");
    
    strcpy(g_sys_config.security.admin_password, "admin");
    strcpy(g_sys_config.security.user_password, "user");
    
    g_sys_config.mqtt.enabled = true;
    strcpy(g_sys_config.mqtt.broker_url, "broker.hivemq.com");
    g_sys_config.mqtt.port = 1883;
    strcpy(g_sys_config.mqtt.base_topic, "synaptix/gateway/data");
    g_sys_config.mqtt.username[0] = '\0';
    g_sys_config.mqtt.password[0] = '\0';
    g_sys_config.mqtt.qos = 1U;
    g_sys_config.mqtt.client_id[0] = '\0';
    g_sys_config.mqtt.keepalive_s = 60U;
    g_sys_config.mqtt.clean_session = true;
    strcpy(g_sys_config.mqtt.publish_strategy, "per_device");
    strcpy(g_sys_config.mqtt.publish_topic, "synaptix/gateway/data");
    g_sys_config.mqtt.publish_interval_ms = 5000U;
    g_sys_config.mqtt.publish_retain = false;
    strcpy(g_sys_config.mqtt.payload_mode, "builder");
    strcpy(g_sys_config.mqtt.payload_template, "{}");
    g_sys_config.mqtt.payload_field_count = 0U;
    
    strcpy(g_sys_config.ntp.server, "pool.ntp.org");
    g_sys_config.ntp.timezone = 7;
}

static char *config_serialize(bool include_secrets) {
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        LOGE(TAG, "config_serialize: cJSON_CreateObject root failed");
        return NULL;
    }
    
    sys_config_lock();
    cJSON_AddNumberToObject(root, "schema_version", CONFIG_SCHEMA_VERSION);
    cJSON_AddNumberToObject(root, "firmware_api", 1);
    cJSON_AddStringToObject(root, "contract_version", CONFIG_CONTRACT_VERSION);
    cJSON_AddNumberToObject(root, "generation", s_config_generation);
    cJSON *device = cJSON_CreateObject();
    cJSON_AddStringToObject(device, "name", g_sys_config.device.device_name);
    cJSON_AddStringToObject(device, "id", g_sys_config.device.device_id);
    /* Compatibility alias for older Web Config bundles. */
    cJSON_AddStringToObject(device, "serial", g_sys_config.device.device_id);
    cJSON_AddItemToObject(root, "device", device);
    
    cJSON *security = cJSON_CreateObject();
    cJSON_AddBoolToObject(security, "admin_password_set", g_sys_config.security.admin_password[0] != '\0');
    cJSON_AddBoolToObject(security, "user_password_set", g_sys_config.security.user_password[0] != '\0');
    if (include_secrets) {
        cJSON_AddStringToObject(security, "admin_pass", g_sys_config.security.admin_password);
        cJSON_AddStringToObject(security, "user_pass", g_sys_config.security.user_password);
    }
    cJSON_AddItemToObject(root, "security", security);
    
    cJSON *mqtt = cJSON_CreateObject();
    cJSON_AddBoolToObject(mqtt, "enabled", g_sys_config.mqtt.enabled);
    cJSON_AddStringToObject(mqtt, "broker", g_sys_config.mqtt.broker_url);
    cJSON_AddNumberToObject(mqtt, "port", g_sys_config.mqtt.port);
    cJSON_AddStringToObject(mqtt, "topic", g_sys_config.mqtt.base_topic);
    cJSON_AddStringToObject(mqtt, "username", g_sys_config.mqtt.username);
    cJSON_AddBoolToObject(mqtt, "password_set", g_sys_config.mqtt.password[0] != '\0');
    if (include_secrets) {
        cJSON_AddStringToObject(mqtt, "password", g_sys_config.mqtt.password);
    }
    cJSON_AddNumberToObject(mqtt, "qos", g_sys_config.mqtt.qos);
    cJSON_AddStringToObject(mqtt, "client_id", g_sys_config.mqtt.client_id);
    cJSON_AddNumberToObject(mqtt, "keepalive_s", g_sys_config.mqtt.keepalive_s);
    cJSON_AddBoolToObject(mqtt, "clean_session", g_sys_config.mqtt.clean_session);
    cJSON *publishers = cJSON_CreateArray();
    cJSON *publish = cJSON_CreateObject();
    cJSON_AddStringToObject(publish, "strategy", g_sys_config.mqtt.publish_strategy);
    /* Dashboard-facing alias for the publish cadence. */
    cJSON_AddStringToObject(publish, "publish_mode", g_sys_config.mqtt.publish_strategy);
    cJSON_AddStringToObject(publish, "topic", g_sys_config.mqtt.publish_topic);
    cJSON_AddNumberToObject(publish, "interval_ms", g_sys_config.mqtt.publish_interval_ms);
    cJSON_AddBoolToObject(publish, "retain", g_sys_config.mqtt.publish_retain);
    cJSON_AddItemToArray(publishers, publish);
    cJSON_AddItemToObject(mqtt, "publishers", publishers);
    /* For backwards compatibility, still send publish object */
    cJSON *publish_compat = cJSON_CreateObject();
    cJSON_AddStringToObject(publish_compat, "strategy", g_sys_config.mqtt.publish_strategy);
    cJSON_AddStringToObject(publish_compat, "publish_mode", g_sys_config.mqtt.publish_strategy);
    cJSON_AddStringToObject(publish_compat, "topic", g_sys_config.mqtt.publish_topic);
    cJSON_AddNumberToObject(publish_compat, "interval_ms", g_sys_config.mqtt.publish_interval_ms);
    cJSON_AddBoolToObject(publish_compat, "retain", g_sys_config.mqtt.publish_retain);
    cJSON_AddItemToObject(mqtt, "publish", publish_compat);
    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "mode", g_sys_config.mqtt.payload_mode);
    cJSON_AddStringToObject(payload, "template", g_sys_config.mqtt.payload_template);
    cJSON *fields = cJSON_AddArrayToObject(payload, "fields");
    for (uint8_t i = 0U; i < g_sys_config.mqtt.payload_field_count; ++i) {
        cJSON *field = cJSON_CreateObject();
        cJSON_AddStringToObject(field, "path", g_sys_config.mqtt.payload_fields[i].path);
        cJSON_AddStringToObject(field, "source", g_sys_config.mqtt.payload_fields[i].source);
        cJSON_AddStringToObject(field, "type", g_sys_config.mqtt.payload_fields[i].type);
        cJSON_AddItemToArray(fields, field);
    }
    cJSON_AddItemToObject(mqtt, "payload", payload);
    cJSON_AddItemToObject(root, "mqtt", mqtt);
    
    // Invoke all registered callbacks to let them attach their nodes
    for (int i = 0; i < s_node_count; i++) {
        if (s_config_nodes[i].save) {
            s_config_nodes[i].save(root, include_secrets);
        }
    }
    sys_config_unlock();
    
    char *json_str = cJSON_PrintUnformatted(root);
    if (!json_str) {
        LOGE(TAG, "config_serialize: cJSON_PrintUnformatted failed");
    }
    cJSON_Delete(root);
    return json_str;
}

char* config_get_json_string(void) {
    return config_serialize(false);
}

int config_save(void) {
    char *json_str = config_serialize(true);
    if (!json_str) {
        return -1;
    }
    
    (void)file_io_mkdir(CONFIG_DIR);
    int len = strlen(json_str);
    int write_ret = file_io_write(CONFIG_FILE_TMP, json_str, len);
    int ret = write_ret;
    if (write_ret == len) ret = file_io_rename(CONFIG_FILE_TMP, CONFIG_FILE);
    cJSON_free(json_str);
    
    if (ret == 0) {
        LOGI(TAG, "Saved successfully (%d bytes)", len);
        return 0;
    } else {
        LOGE(TAG, "Failed to save file %s: %d", CONFIG_FILE, ret);
        return -1;
    }
}

static bool config_string_fits(const cJSON *item, size_t max_len) {
    return item == NULL || (cJSON_IsString(item) && strlen(item->valuestring) < max_len);
}

static int config_parse_json_n(const char *json_str, size_t json_len) {
    cJSON *root = cJSON_ParseWithLength(json_str, json_len);
    if (!root) {
        config_set_error("", "invalid_json", "Configuration is not valid JSON");
        LOGE(TAG, "JSON parse failed");
        return -1;
    }

    cJSON *schema = cJSON_GetObjectItem(root, "schema_version");
    if (cJSON_IsNumber(schema) && schema->valueint > (int)CONFIG_SCHEMA_VERSION) {
        config_set_error("schema_version", "unsupported_schema", "Configuration schema is newer than this firmware");
        LOGE(TAG, "Unsupported config schema: %d", schema->valueint);
        cJSON_Delete(root);
        return -1;
    }
    cJSON *device_check = cJSON_GetObjectItem(root, "device");
    cJSON *mqtt_check = cJSON_GetObjectItem(root, "mqtt");
    if (device_check && (!config_string_fits(cJSON_GetObjectItem(device_check, "name"), sizeof(s_staging_config.device.device_name)) ||
                         !config_string_fits(cJSON_GetObjectItem(device_check, "id"), sizeof(s_staging_config.device.device_id)) ||
                         !config_string_fits(cJSON_GetObjectItem(device_check, "serial"), sizeof(s_staging_config.device.device_id)))) {
        config_set_error("device", "invalid_length", "Device field exceeds firmware limit");
        cJSON_Delete(root); return -1;
    }
    if (mqtt_check) {
        cJSON *port = cJSON_GetObjectItem(mqtt_check, "port");
        cJSON *qos = cJSON_GetObjectItem(mqtt_check, "qos");
        if ((port && (!cJSON_IsNumber(port) || port->valueint < 1 || port->valueint > 65535)) ||
            (qos && (!cJSON_IsNumber(qos) || qos->valueint < 0 || qos->valueint > 2)) ||
            !config_string_fits(cJSON_GetObjectItem(mqtt_check, "broker"), sizeof(s_staging_config.mqtt.broker_url)) ||
            !config_string_fits(cJSON_GetObjectItem(mqtt_check, "topic"), sizeof(s_staging_config.mqtt.base_topic)) ||
            !config_string_fits(cJSON_GetObjectItem(mqtt_check, "username"), sizeof(s_staging_config.mqtt.username)) ||
            !config_string_fits(cJSON_GetObjectItem(mqtt_check, "password"), sizeof(s_staging_config.mqtt.password))) {
            config_set_error("mqtt", "invalid_value", "MQTT field is invalid or exceeds firmware limit");
            cJSON_Delete(root); return -1;
        }
    }

    cJSON *base_generation = cJSON_GetObjectItem(root, "base_generation");
    if (cJSON_IsNumber(base_generation) && (uint32_t)base_generation->valuedouble != s_config_generation) {
        config_set_error("base_generation", "conflict", "Configuration was changed by another client");
        cJSON_Delete(root);
        return CONFIG_RESULT_CONFLICT;
    }

    cJSON *modbus = cJSON_GetObjectItem(root, "modbus");
    if (cJSON_IsObject(modbus)) {
        cJSON *ports = cJSON_GetObjectItem(modbus, "ports");
        if (cJSON_IsArray(ports)) {
            cJSON *port = NULL;
            cJSON_ArrayForEach(port, ports) {
                cJSON *mode = cJSON_GetObjectItem(port, "mode");
                if (cJSON_IsString(mode) &&
                    (strcmp(mode->valuestring, "tcp_client") == 0 ||
                     strcmp(mode->valuestring, "modbus_gateway") == 0)) {
                    LOGW(TAG, "Mode %s is parsed but not supported. Task will not be created.", mode->valuestring);
                    // Do NOT reject, allow JSON to parse so boot doesn't fail
                }
            }
        }
    }

    if (cJSON_IsObject(mqtt_check)) {
        cJSON *publishers = cJSON_GetObjectItem(mqtt_check, "publishers");
        if (cJSON_IsArray(publishers) && cJSON_GetArraySize(publishers) > 1) {
            LOGW(TAG, "Multiple MQTT publishers are not enabled in this firmware, ignoring additional publishers...");
        }
    }
    
    sys_config_lock();
    cJSON *device = cJSON_GetObjectItem(root, "device");
    if (device) {
        cJSON *name = cJSON_GetObjectItem(device, "name");
        if (cJSON_IsString(name)) {
            snprintf(s_staging_config.device.device_name, sizeof(s_staging_config.device.device_name), "%s", name->valuestring);
        }
        cJSON *id = cJSON_GetObjectItem(device, "id");
        if (!cJSON_IsString(id)) id = cJSON_GetObjectItem(device, "serial");
        if (cJSON_IsString(id)) {
            snprintf(s_staging_config.device.device_id, sizeof(s_staging_config.device.device_id), "%s", id->valuestring);
        }
    }
    
    cJSON *security = cJSON_GetObjectItem(root, "security");
    if (security) {
        cJSON *admin_pass = cJSON_GetObjectItem(security, "admin_pass");
        if (cJSON_IsString(admin_pass)) {
            snprintf(s_staging_config.security.admin_password, sizeof(s_staging_config.security.admin_password), "%s", admin_pass->valuestring);
        }
        cJSON *user_pass = cJSON_GetObjectItem(security, "user_pass");
        if (cJSON_IsString(user_pass)) {
            snprintf(s_staging_config.security.user_password, sizeof(s_staging_config.security.user_password), "%s", user_pass->valuestring);
        }
    }
    
    cJSON *mqtt = cJSON_GetObjectItem(root, "mqtt");
    if (mqtt) {
        cJSON *enabled = cJSON_GetObjectItem(mqtt, "enabled");
        if (cJSON_IsBool(enabled)) s_staging_config.mqtt.enabled = cJSON_IsTrue(enabled);
        cJSON *broker = cJSON_GetObjectItem(mqtt, "broker");
        if (cJSON_IsString(broker)) {
            snprintf(s_staging_config.mqtt.broker_url, sizeof(s_staging_config.mqtt.broker_url), "%s", broker->valuestring);
        }
        cJSON *port = cJSON_GetObjectItem(mqtt, "port");
        if (cJSON_IsNumber(port)) s_staging_config.mqtt.port = port->valueint;
        cJSON *topic = cJSON_GetObjectItem(mqtt, "topic");
        if (cJSON_IsString(topic)) {
            snprintf(s_staging_config.mqtt.base_topic, sizeof(s_staging_config.mqtt.base_topic), "%s", topic->valuestring);
        }
        cJSON *username = cJSON_GetObjectItem(mqtt, "username");
        if (cJSON_IsString(username)) {
            snprintf(s_staging_config.mqtt.username, sizeof(s_staging_config.mqtt.username), "%s", username->valuestring);
        }
        cJSON *password = cJSON_GetObjectItem(mqtt, "password");
        if (cJSON_IsString(password) && password->valuestring[0] != '\0') {
            snprintf(s_staging_config.mqtt.password, sizeof(s_staging_config.mqtt.password), "%s", password->valuestring);
        }
        cJSON *qos = cJSON_GetObjectItem(mqtt, "qos");
        if (cJSON_IsNumber(qos) && qos->valueint >= 0 && qos->valueint <= 2)
            s_staging_config.mqtt.qos = (uint8_t)qos->valueint;
        cJSON *client_id = cJSON_GetObjectItem(mqtt, "client_id");
        if (cJSON_IsString(client_id))
            snprintf(s_staging_config.mqtt.client_id, sizeof(s_staging_config.mqtt.client_id), "%s", client_id->valuestring);
        cJSON *keepalive = cJSON_GetObjectItem(mqtt, "keepalive_s");
        if (cJSON_IsNumber(keepalive) && keepalive->valueint >= 10 && keepalive->valueint <= 3600)
            s_staging_config.mqtt.keepalive_s = (uint16_t)keepalive->valueint;
        cJSON *clean_session = cJSON_GetObjectItem(mqtt, "clean_session");
        if (cJSON_IsBool(clean_session)) s_staging_config.mqtt.clean_session = cJSON_IsTrue(clean_session);
        cJSON *publish = cJSON_GetObjectItem(mqtt, "publish");
        if (!publish) {
            cJSON *publishers = cJSON_GetObjectItem(mqtt, "publishers");
            if (cJSON_IsArray(publishers) && cJSON_GetArraySize(publishers) > 0) {
                publish = cJSON_GetArrayItem(publishers, 0);
            }
        }
        if (cJSON_IsObject(publish)) {

            cJSON *strategy = cJSON_GetObjectItem(publish, "strategy");
            /* Accept the dashboard's publish_mode key as an alias. */
            if (!cJSON_IsString(strategy) || strategy->valuestring[0] == '\0')
                strategy = cJSON_GetObjectItem(publish, "publish_mode");
            cJSON *publish_topic = cJSON_GetObjectItem(publish, "topic");
            cJSON *interval = cJSON_GetObjectItem(publish, "interval_ms");
            cJSON *retain = cJSON_GetObjectItem(publish, "retain");
            if (cJSON_IsString(strategy) && strategy->valuestring[0] != '\0')
                snprintf(s_staging_config.mqtt.publish_strategy, sizeof(s_staging_config.mqtt.publish_strategy), "%s", strategy->valuestring);
            if (cJSON_IsString(publish_topic))
                snprintf(s_staging_config.mqtt.publish_topic, sizeof(s_staging_config.mqtt.publish_topic), "%s", publish_topic->valuestring);
            if (cJSON_IsNumber(interval) && interval->valueint >= 100 && interval->valueint <= 86400000)
                s_staging_config.mqtt.publish_interval_ms = (uint32_t)interval->valueint;
            if (cJSON_IsBool(retain)) s_staging_config.mqtt.publish_retain = cJSON_IsTrue(retain);
        }
        cJSON *payload = cJSON_GetObjectItem(mqtt, "payload");
        if (cJSON_IsObject(payload)) {
            cJSON *mode = cJSON_GetObjectItem(payload, "mode");
            cJSON *template = cJSON_GetObjectItem(payload, "template");
            if (cJSON_IsString(mode))
                snprintf(s_staging_config.mqtt.payload_mode, sizeof(s_staging_config.mqtt.payload_mode), "%s", mode->valuestring);
            if (cJSON_IsString(template))
                snprintf(s_staging_config.mqtt.payload_template, sizeof(s_staging_config.mqtt.payload_template), "%s", template->valuestring);
            cJSON *fields = cJSON_GetObjectItem(payload, "fields");
            if (cJSON_IsArray(fields)) {
                s_staging_config.mqtt.payload_field_count = 0U;
                cJSON *field = NULL;
                cJSON_ArrayForEach(field, fields) {
                    if (s_staging_config.mqtt.payload_field_count >= MQTT_PAYLOAD_MAX_FIELDS || !cJSON_IsObject(field)) break;
                    cJSON *path = cJSON_GetObjectItem(field, "path");
                    cJSON *source = cJSON_GetObjectItem(field, "source");
                    cJSON *type = cJSON_GetObjectItem(field, "type");
                    if (!cJSON_IsString(path) || !cJSON_IsString(source) || !cJSON_IsString(type)) continue;
                    mqtt_payload_field_t *dst = &s_staging_config.mqtt.payload_fields[s_staging_config.mqtt.payload_field_count++];
                    snprintf(dst->path, sizeof(dst->path), "%s", path->valuestring);
                    snprintf(dst->source, sizeof(dst->source), "%s", source->valuestring);
                    snprintf(dst->type, sizeof(dst->type), "%s", type->valuestring);
                }
            }
        }
    }
    
    // Invoke all registered callbacks to let them parse their nodes
    for (int i = 0; i < s_node_count; i++) {
        if (s_config_nodes[i].load) {
            cJSON *node = cJSON_GetObjectItem(root, s_config_nodes[i].name);
            s_config_nodes[i].load(node);
        }
    }
    sys_config_unlock();

    cJSON_Delete(root);
    return 0;
}

static int config_parse_json(const char *json_str) {
    return json_str == NULL ? -1 : config_parse_json_n(json_str, strlen(json_str));
}

int config_load(void) {
    if (!s_config_mutex) {
        s_config_mutex = xSemaphoreCreateMutex();
    }
    if (!s_config_transaction_mutex) {
        s_config_transaction_mutex = xSemaphoreCreateMutex();
    }
    config_clear_error();
    /* Seed defaults before parsing so older configuration files inherit all
     * fields introduced by newer firmware versions. */
    config_set_default();
    
    int file_size = file_io_size(CONFIG_FILE);
    const char *load_path = CONFIG_FILE;
    if (file_size <= 0) {
        file_size = file_io_size(LEGACY_CONFIG_FILE);
        load_path = LEGACY_CONFIG_FILE;
    }
    if (file_size <= 0) {
        LOGW(TAG, "No system.json or empty, loading defaults");
        config_set_default();
        return -1;
    }

    char *json_str = pvPortMalloc(file_size + 1);
    if (!json_str) {
        LOGE(TAG, "Failed to allocate memory for config");
        config_set_default();
        return -1;
    }
    
    int ret = file_io_read(load_path, json_str, file_size);
    if (ret <= 0) {
        LOGW(TAG, "Failed to read system.json, loading defaults");
        vPortFree(json_str);
        config_set_default();
        return -1;
    }
    json_str[ret] = '\0'; // Ensure null-termination
    
    if (config_parse_json(json_str) != 0) {
        config_set_default();
        vPortFree(json_str);
        return -1;
    }
    
    config_manager_commit_all();
    vPortFree(json_str);
    s_config_generation++;
    LOGI(TAG, "Loaded successfully");
    return 0;
}

int config_update_from_json_n(const char *json_str, size_t json_len) {
    if (!json_str || json_len == 0U || json_len > CONFIG_MAX_JSON_SIZE) {
        config_set_error("", "body_too_large", "Configuration body is empty or exceeds 16 KiB");
        return CONFIG_RESULT_INVALID;
    }
    if (!s_config_transaction_mutex) s_config_transaction_mutex = xSemaphoreCreateMutex();
    if (xSemaphoreTake(s_config_transaction_mutex, pdMS_TO_TICKS(5000U)) != pdTRUE) {
        config_set_error("", "busy", "Another configuration update is in progress");
        return CONFIG_RESULT_INVALID;
    }

    config_clear_error();
    char *previous = config_serialize(true);
    if (previous == NULL) {
        config_set_error("", "snapshot_failed", "Unable to snapshot current configuration");
        xSemaphoreGive(s_config_transaction_mutex);
        return CONFIG_RESULT_STORAGE;
    }

    memcpy(&s_staging_config, &g_sys_config, sizeof(sys_config_t));
    int ret = config_parse_json_n(json_str, json_len);
    if (ret == 0) {
        config_manager_commit_all();
        ret = config_save() == 0 ? CONFIG_RESULT_OK : CONFIG_RESULT_STORAGE;
    }
    if (ret != CONFIG_RESULT_OK) {
        /* no rollback needed, staging is discarded */
        if (ret == CONFIG_RESULT_STORAGE) config_set_error("", "storage_error", "Unable to persist configuration");
    } else {
        s_config_generation++;
        extern void config_apply_manager_request(uint32_t generation);
        config_apply_manager_request(s_config_generation);
    }
    cJSON_free(previous);
    xSemaphoreGive(s_config_transaction_mutex);
    return ret;
}

int config_update_from_json(const char *json_str) {
    return json_str == NULL ? -1 : config_update_from_json_n(json_str, strlen(json_str));
}

void sys_config_lock(void) {
    if (s_config_mutex) xSemaphoreTake(s_config_mutex, portMAX_DELAY);
}

void sys_config_unlock(void) {
    if (s_config_mutex) xSemaphoreGive(s_config_mutex);
}

uint32_t config_get_generation(void) { return s_config_generation; }
const char *config_get_contract_version(void) { return CONFIG_CONTRACT_VERSION; }
const char *config_get_last_error_path(void) { return s_last_error_path; }
const char *config_get_last_error_code(void) { return s_last_error_code; }
const char *config_get_last_error_message(void) { return s_last_error_message; }


sys_config_t* config_get_staging(void) { return &s_staging_config; }

void config_manager_commit_all(void) {
    sys_config_lock();
    if (memcmp(&g_sys_config.mqtt, &s_staging_config.mqtt, sizeof(mqtt_config_t)) != 0) {
        g_mqtt_config_changed = true;
    } else {
        g_mqtt_config_changed = false;
    }
    memcpy(&g_sys_config, &s_staging_config, sizeof(sys_config_t));
    for (int i = 0; i < s_node_count; i++) {
        if (s_config_nodes[i].commit) s_config_nodes[i].commit();
    }
    sys_config_unlock();
}

int config_manager_get_node_count(void) { return s_node_count; }
config_node_t *config_manager_get_node(int index) { return (index >= 0 && index < s_node_count) ? &s_config_nodes[index] : NULL; }

void config_manager_notify_all(void) {
    sys_config_lock();
    for (int i = 0; i < s_node_count; i++) {
        if (s_config_nodes[i].notify) s_config_nodes[i].notify();
    }
    sys_config_unlock();
}
