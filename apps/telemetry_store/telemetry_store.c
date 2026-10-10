#include "stm32h5xx.h"
/**
 * @file telemetry_store.c
 * @brief Telemetry Store implementation using SeqLock
 */
#include "telemetry_store.h"
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"

#define MAX_TELEMETRY_POINTS 256U
#define MAX_KEY_LEN 64U

typedef struct {
    char key[MAX_KEY_LEN];
    telemetry_value_type_t type;
    telemetry_value_t value;
    telemetry_quality_t quality;
    uint32_t timestamp;
    volatile uint32_t sequence; /* volatile to prevent compiler reordering/caching */
} telemetry_record_t;

static telemetry_record_t s_store[MAX_TELEMETRY_POINTS];
static uint16_t s_point_count = 0;


void telemetry_store_init(void) {
    memset(s_store, 0, sizeof(s_store));
    s_point_count = 0;
}

void telemetry_store_clear(void) {
    taskENTER_CRITICAL();
    memset(s_store, 0, sizeof(s_store));
    s_point_count = 0;
    taskEXIT_CRITICAL();
}

int telemetry_store_register_point(const char *key, telemetry_value_type_t type, telemetry_id_t *out_id) {
    if (!key || !out_id) return -1;
    
    taskENTER_CRITICAL();
    
    /* Check if already exists */
    for (uint16_t i = 0; i < s_point_count; i++) {
        if (strncmp(s_store[i].key, key, MAX_KEY_LEN) == 0) {
            if (s_store[i].type != type) {
                taskEXIT_CRITICAL();
                return -2; /* Type mismatch */
            }
            *out_id = i;
            taskEXIT_CRITICAL();
            return 0;
        }
    }
    
    /* Register new */
    if (s_point_count >= MAX_TELEMETRY_POINTS) {
        taskEXIT_CRITICAL();
        return -3; /* Full */
    }
    
    uint16_t idx = s_point_count++;
    strncpy(s_store[idx].key, key, MAX_KEY_LEN - 1);
    s_store[idx].key[MAX_KEY_LEN - 1] = '\0';
    s_store[idx].type = type;
    s_store[idx].quality = TELEMETRY_VALUE_INVALID;
    s_store[idx].timestamp = 0;
    s_store[idx].sequence = 0;
    
    *out_id = idx;
    taskEXIT_CRITICAL();
    
    return 0;
}

telemetry_id_t telemetry_store_lookup(const char *key) {
    if (!key) return TELEMETRY_INVALID_ID;
    
    taskENTER_CRITICAL();
    for (uint16_t i = 0; i < s_point_count; i++) {
        if (strncmp(s_store[i].key, key, MAX_KEY_LEN) == 0) {
            taskEXIT_CRITICAL();
            return i;
        }
    }
    taskEXIT_CRITICAL();
    return TELEMETRY_INVALID_ID;
}

int telemetry_store_update(telemetry_id_t id, const telemetry_value_t *value, telemetry_quality_t quality, uint32_t timestamp) {
    if (id >= s_point_count || !value) return -1;
    
    telemetry_record_t *rec = &s_store[id];
    
    if (rec->type != value->type) return -2;

    /* Write Seqlock: sequence becomes odd */
    uint32_t seq = rec->sequence;
    seq++;
    rec->sequence = seq;
    
    /* Memory barrier to ensure sequence is written before data */
    __DSB();
    
    rec->value = *value;
    rec->quality = quality;
    rec->timestamp = timestamp;
    
    /* Memory barrier to ensure data is written before sequence is updated */
    __DSB();
    
    /* Write Seqlock: sequence becomes even */
    rec->sequence = seq + 1;
    
    return 0;
}

int telemetry_store_set_quality(telemetry_id_t id, telemetry_quality_t quality, uint32_t timestamp) {
    if (id >= s_point_count) return -1;
    
    telemetry_record_t *rec = &s_store[id];
    
    /* Write Seqlock */
    uint32_t seq = rec->sequence;
    seq++;
    rec->sequence = seq;
    
    __DSB();
    
    rec->quality = quality;
    rec->timestamp = timestamp;
    
    __DSB();
    
    rec->sequence = seq + 1;
    
    return 0;
}

int telemetry_store_get(telemetry_id_t id, telemetry_point_snapshot_t *out) {
    if (id >= s_point_count || !out) return -1;
    
    telemetry_record_t *rec = &s_store[id];
    uint32_t seq_a, seq_b;
    
    do {
        seq_a = rec->sequence;
        
        /* If odd, writer is currently updating. Yield and retry. */
        if (seq_a & 1) {
            taskYIELD();
            continue;
        }
        
        __DSB(); /* Memory barrier */
        
        out->id = id;
        out->value = rec->value;
        out->quality = rec->quality;
        out->timestamp = rec->timestamp;
        
        __DSB(); /* Memory barrier */
        
        seq_b = rec->sequence;
    } while (seq_a != seq_b || (seq_a & 1));
    
    out->sequence = seq_b;
    return 0;
}
const char* telemetry_store_get_key(telemetry_id_t id) {
    if (id >= s_point_count) return NULL;
    return s_store[id].key;
}
