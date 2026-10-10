/**
 * @file telemetry_store.h
 * @brief Thread-safe lock-free telemetry store using SeqLock
 */
#ifndef __TELEMETRY_STORE_H__
#define __TELEMETRY_STORE_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint16_t telemetry_id_t;

#define TELEMETRY_INVALID_ID 0xFFFFU

typedef enum {
    TELEMETRY_VALUE_INVALID = 0,
    TELEMETRY_VALUE_GOOD,
    TELEMETRY_VALUE_STALE,
    TELEMETRY_VALUE_TIMEOUT,
    TELEMETRY_VALUE_ERROR
} telemetry_quality_t;

typedef enum {
    TELEMETRY_TYPE_INT32 = 0,
    TELEMETRY_TYPE_UINT32,
    TELEMETRY_TYPE_FLOAT,
    TELEMETRY_TYPE_BOOL
} telemetry_value_type_t;

typedef struct {
    telemetry_value_type_t type;
    union {
        int32_t i32;
        uint32_t u32;
        float f32;
        bool boolean;
    } value;
} telemetry_value_t;

typedef struct {
    telemetry_id_t id;
    telemetry_value_t value;
    telemetry_quality_t quality;
    uint32_t timestamp;
    uint32_t sequence; /* Used for SeqLock */
} telemetry_point_snapshot_t;

/**
 * @brief Initialize the telemetry store
 */
void telemetry_store_init(void);

/**
 * @brief Clear all telemetry points
 */
void telemetry_store_clear(void);

/**
 * @brief Register a new telemetry point
 * @param key The unique string key (e.g., "meter.voltage")
 * @param type The value type
 * @param out_id Pointer to store the assigned ID
 * @return 0 on success, < 0 on error
 */
int telemetry_store_register_point(const char *key, telemetry_value_type_t type, telemetry_id_t *out_id);

/**
 * @brief Lookup an existing point ID by key
 * @param key The unique string key
 * @return The ID, or TELEMETRY_INVALID_ID if not found
 */
telemetry_id_t telemetry_store_lookup(const char *key);

/**
 * @brief Update a point's value and quality (Writer)
 * @param id The telemetry ID
 * @param value The new value
 * @param quality The new quality
 * @param timestamp The timestamp (usually system uptime in ms)
 * @return 0 on success, < 0 on error
 */
int telemetry_store_update(telemetry_id_t id, const telemetry_value_t *value, telemetry_quality_t quality, uint32_t timestamp);

/**
 * @brief Update only a point's quality and timestamp (e.g. on timeout) (Writer)
 * @param id The telemetry ID
 * @param quality The new quality
 * @param timestamp The timestamp
 * @return 0 on success, < 0 on error
 */
int telemetry_store_set_quality(telemetry_id_t id, telemetry_quality_t quality, uint32_t timestamp);

/**
 * @brief Get a consistent snapshot of a point (Reader)
 * @param id The telemetry ID
 * @param out The output snapshot
 * @return 0 on success, < 0 on error
 */
int telemetry_store_get(telemetry_id_t id, telemetry_point_snapshot_t *out);

const char* telemetry_store_get_key(telemetry_id_t id);

const char* telemetry_store_get_key(telemetry_id_t id);

#ifdef __cplusplus
}
#endif

#endif /* __TELEMETRY_STORE_H__ */