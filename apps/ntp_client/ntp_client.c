#include "ntp_client.h"
#include "logger.h"
#include "config_manager.h"
#include "lwip/apps/sntp.h"
#include "lwip/tcpip.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

static const char *TAG = "NTP";

static uint32_t g_ntp_base_unix = 0;
static uint32_t g_ntp_base_tick = 0;
static bool g_ntp_synced = false;

/* This function is called by LwIP SNTP module when time is received */
void sntp_set_time(uint32_t sntp_time) {
    taskENTER_CRITICAL();
    g_ntp_base_unix = sntp_time;
    g_ntp_base_tick = xTaskGetTickCount();
    g_ntp_synced = true;
    taskEXIT_CRITICAL();
    
    LOGI(TAG, "Time synchronized: %lu (Unix Timestamp)", sntp_time);
}

static bool s_ntp_changed = false;

static void ntp_config_load(cJSON *node) {
    if (!node) return;
    cJSON *server = cJSON_GetObjectItem(node, "server");
    if (cJSON_IsString(server) && strcmp(config_get_staging()->ntp.server, server->valuestring) != 0) {
        strncpy(config_get_staging()->ntp.server, server->valuestring, 63);
        s_ntp_changed = true;
    }
    cJSON *timezone = cJSON_GetObjectItem(node, "timezone");
    if (cJSON_IsNumber(timezone) && config_get_staging()->ntp.timezone != timezone->valueint) {
        config_get_staging()->ntp.timezone = timezone->valueint;
        s_ntp_changed = true;
    }
}

static void ntp_config_save(cJSON *root, bool include_secrets) {
    (void)include_secrets;
    if (!root) return;
    cJSON *ntp = cJSON_CreateObject();
    cJSON_AddStringToObject(ntp, "server", g_sys_config.ntp.server);
    cJSON_AddNumberToObject(ntp, "timezone", g_sys_config.ntp.timezone);
    cJSON_AddItemToObject(root, "ntp", ntp);
}

static 
int ntp_config_apply(void) {
    if (!s_ntp_changed) {
        LOGI("NTP", "NTP configuration unchanged, skipping apply");
        return 0;
    }
    LOGI("NTP", "Applying NTP config...");
    extern void ntp_config_notify(void);
    ntp_config_notify();
    s_ntp_changed = false;
    return 0;
}
void ntp_config_notify(void) {
    ntp_client_sync();
}

void ntp_client_init(void) {
    LOGI(TAG, "Initializing NTP Client...");
    
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    
    if (strlen(g_sys_config.ntp.server) > 0) {
        sntp_setservername(0, g_sys_config.ntp.server);
        LOGI(TAG, "NTP Server configured: %s", g_sys_config.ntp.server);
    } else {
        sntp_setservername(0, "pool.ntp.org");
        LOGI(TAG, "NTP Server default: pool.ntp.org");
    }
    
    sntp_init();
}

void ntp_config_register(void) {
    config_manager_register_node("ntp", ntp_config_load, ntp_config_save, ntp_config_notify, NULL, ntp_config_apply);
}

static void ntp_client_restart_tcpip_cb(void *ctx) {
    (void)ctx;
    sntp_stop();
    if (strlen(g_sys_config.ntp.server) > 0) {
        sntp_setservername(0, g_sys_config.ntp.server);
    } else {
        sntp_setservername(0, "pool.ntp.org");
    }
    sntp_init();
}

void ntp_client_sync(void) {
    LOGI(TAG, "Forcing NTP time sync (via tcpip thread)...");
    tcpip_callback(ntp_client_restart_tcpip_cb, NULL);
}

bool ntp_is_synced(void) {
    return g_ntp_synced;
}

uint32_t ntp_get_unix_time(void) {
    if (!g_ntp_synced) {
        return 0; /* Return Unix Time 0 (1970) if not synced */
    }
    
    taskENTER_CRITICAL();
    uint32_t base_unix = g_ntp_base_unix;
    uint32_t base_tick = g_ntp_base_tick;
    taskEXIT_CRITICAL();
    
    uint32_t current_tick = xTaskGetTickCount();
    uint32_t elapsed_ticks = current_tick - base_tick;
    uint32_t elapsed_seconds = elapsed_ticks / configTICK_RATE_HZ;
    
    /* Apply timezone offset from config */
    sys_config_lock();
    int32_t tz_offset_sec = g_sys_config.ntp.timezone * 3600;
    sys_config_unlock();
    
    return base_unix + elapsed_seconds + tz_offset_sec;
}
