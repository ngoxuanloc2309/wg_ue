#include "gateway.h"
#include "config_apply_manager.h"
#include "config_manager.h"
#include "logger.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include <string.h>
#include <stdio.h>

#define TAG "CONFIG_APPLY"

static config_apply_status_t s_apply_status;
static QueueHandle_t s_apply_queue;
static TaskHandle_t s_apply_task_handle;

extern void app_mqtt_request_reconnect(void);
extern int config_manager_get_node_count(void);
extern config_node_t *config_manager_get_node(int index);

static void config_apply_task(void *pvParameters) {
    uint32_t req_generation;
    while (1) {
        if (xQueueReceive(s_apply_queue, &req_generation, portMAX_DELAY) == pdTRUE) {
            LOGI(TAG, "Starting apply for generation: %lu", (unsigned long)req_generation);
            s_apply_status.state = APPLY_STATE_APPLYING;
            
            uint32_t latest_generation = req_generation;
            while (xQueueReceive(s_apply_queue, &req_generation, 0) == pdTRUE) {
                latest_generation = req_generation;
                LOGW(TAG, "Skipping older generation in favor of %lu", (unsigned long)latest_generation);
            }
            
            bool has_error = false;
            
            int count = config_manager_get_node_count();
            for (int i = 0; i < count; i++) {
                config_node_t *node = config_manager_get_node(i);
                if (node && node->apply) {
                    LOGI(TAG, "Applying node: %s", node->name);
                    int res = node->apply();
                    if (res != 0) {
                        has_error = true;
                        LOGE(TAG, "Node %s failed to apply (error %d)", node->name, res);
                        snprintf(s_apply_status.last_error.module, sizeof(s_apply_status.last_error.module), "%s", node->name);
                        s_apply_status.last_error.port = 0;
                        snprintf(s_apply_status.last_error.code, sizeof(s_apply_status.last_error.code), "err_%d", res);
                        break;
                    }
                }
            }
            
            if (has_error) {
                s_apply_status.state = APPLY_STATE_ERROR;
                gateway_set_state(GW_STATE_ERROR);
                LOGE(TAG, "Apply failed for generation: %lu", (unsigned long)latest_generation);
            } else {
                LOGI(TAG, "Applying MQTT config...");
                app_mqtt_request_reconnect();
                
                memset(&s_apply_status.last_error, 0, sizeof(config_apply_error_t));
                s_apply_status.applied_generation = latest_generation;
                s_apply_status.state = APPLY_STATE_APPLIED;
                gateway_set_state(GW_STATE_RUNNING);
                LOGI(TAG, "Successfully applied generation: %lu", (unsigned long)latest_generation);
            }
        }
    }
}

void config_apply_manager_init(void) {
    memset(&s_apply_status, 0, sizeof(s_apply_status));
    s_apply_status.state = APPLY_STATE_PENDING;
    s_apply_status.applied_generation = 0; // Will be set after first successful apply
    s_apply_status.current_generation = config_get_generation();

    s_apply_queue = xQueueCreate(4, sizeof(uint32_t));
    if (s_apply_queue != NULL) {
        xTaskCreate(config_apply_task, "CFG_APPLY", 1024, NULL, configMAX_PRIORITIES - 2, &s_apply_task_handle);
    }
}

void config_apply_manager_request(uint32_t generation) {
    if (s_apply_queue != NULL) {
        s_apply_status.current_generation = generation;
        s_apply_status.state = APPLY_STATE_PENDING;
        xQueueSend(s_apply_queue, &generation, 0);
    }
}

void config_apply_manager_get_status(config_apply_status_t *status) {
    if (status != NULL) {
        memcpy(status, &s_apply_status, sizeof(config_apply_status_t));
    }
}

const char* config_apply_state_to_string(config_apply_state_t state) {
    switch (state) {
        case APPLY_STATE_PENDING: return "pending";
        case APPLY_STATE_APPLYING: return "applying";
        case APPLY_STATE_APPLIED: return "applied";
        case APPLY_STATE_ERROR: return "error";
        default: return "unknown";
    }
}

void config_apply_manager_retry(void) {
    if (s_apply_queue != NULL) {
        s_apply_status.state = APPLY_STATE_PENDING;
        xQueueSend(s_apply_queue, &s_apply_status.current_generation, 0);
    }
}
