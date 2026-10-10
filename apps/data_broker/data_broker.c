/**
 * @file data_broker.c
 * @brief Data Broker implementation
 */
#include "data_broker.h"
#include "logger.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "cJSON.h"
#include "config_manager.h"
#include "mqtt_client.h"
#include "ntp_client.h"
#include "telemetry_store.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "DataBroker";


void data_broker_init(void) {
    LOGI(TAG, "Data Broker initialized");
}

void data_broker_task(void *params) {
    (void)params;

    for (;;) {
        bool mqtt_enabled;
        uint32_t interval_ms;
        char topic[96];
        
        sys_config_lock();
        mqtt_enabled = g_sys_config.mqtt.enabled;
        interval_ms = g_sys_config.mqtt.publish_interval_ms;
        snprintf(topic, sizeof(topic), "%s",
                 g_sys_config.mqtt.publish_topic[0] != '\0'
                   ? g_sys_config.mqtt.publish_topic
                   : g_sys_config.mqtt.base_topic);
        sys_config_unlock();
        
        if (interval_ms < 1000) interval_ms = 1000; /* min 1s */
        
        vTaskDelay(pdMS_TO_TICKS(interval_ms));
        
        if (!mqtt_enabled) continue;
        
        /* Build payload from telemetry store */
        cJSON *payload = cJSON_CreateObject();
        if (payload) {
            uint32_t ts = ntp_get_unix_time();
            if (ts > 0) {
                cJSON_AddNumberToObject(payload, "ts", ts);
            }
            
            cJSON *data_obj = cJSON_CreateObject();
            cJSON_AddItemToObject(payload, "data", data_obj);
            
            /* Iterate through all points */
            for (telemetry_id_t i = 0; i < 256; i++) {
                const char *key = telemetry_store_get_key(i);
                if (!key) break; /* No more points */
                
                telemetry_point_snapshot_t snap;
                if (telemetry_store_get(i, &snap) == 0) {
                    if (snap.quality == TELEMETRY_VALUE_INVALID) continue;
                    
                    if (snap.quality == TELEMETRY_VALUE_TIMEOUT) {
                        /* For Phase 6: We could add quality fields, or just publish the stale value */
                    }
                    
                    if (snap.value.type == TELEMETRY_TYPE_FLOAT) {
                        cJSON_AddNumberToObject(data_obj, key, snap.value.value.f32);
                    } else if (snap.value.type == TELEMETRY_TYPE_INT32) {
                        cJSON_AddNumberToObject(data_obj, key, snap.value.value.i32);
                    } else if (snap.value.type == TELEMETRY_TYPE_UINT32) {
                        cJSON_AddNumberToObject(data_obj, key, snap.value.value.u32);
                    } else if (snap.value.type == TELEMETRY_TYPE_BOOL) {
                        cJSON_AddBoolToObject(data_obj, key, snap.value.value.boolean);
                    }
                }
            }
            
            char *json_str = cJSON_PrintUnformatted(payload);
            if (json_str) {
                LOGI(TAG, "Publishing to %s: %s", topic, json_str);
                mqtt_client_publish(topic, json_str);

                /* Must match the allocator cJSON used (FreeRTOS heap via
                 * cJSON_InitHooks) -- libc free() here corrupts newlib heap
                 * metadata and crashed the firmware inside _free_r(). */
                cJSON_free(json_str);
            }
            cJSON_Delete(payload);
        }
    }
}

int data_broker_publish(const char *topic, cJSON *payload) {
    return -1;
}

