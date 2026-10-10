/**
 * @file config_apply_manager.h
 * @brief Configuration Apply Manager
 */
#ifndef __CONFIG_APPLY_MANAGER_H__
#define __CONFIG_APPLY_MANAGER_H__

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    APPLY_STATE_PENDING = 0,
    APPLY_STATE_APPLYING,
    APPLY_STATE_APPLIED,
    APPLY_STATE_ERROR
} config_apply_state_t;

typedef struct {
    char module[32];
    uint8_t port;
    char code[32];
} config_apply_error_t;

typedef struct {
    uint32_t current_generation;
    uint32_t applied_generation;
    config_apply_state_t state;
    config_apply_error_t last_error;
} config_apply_status_t;

/**
 * @brief Initialize the Apply Manager task and queue
 */
void config_apply_manager_init(void);

/**
 * @brief Enqueue a new apply request for a specific generation
 * @param generation The generation of the configuration to apply
 */
void config_apply_manager_request(uint32_t generation);

/**
 * @brief Retry applying the current generation
 */
void config_apply_manager_retry(void);

/**
 * @brief Get the current apply status
 * @param status Pointer to store the status
 */
void config_apply_manager_get_status(config_apply_status_t *status);

/**
 * @brief Format apply state as a string
 * @param state State enum
 * @return String representation
 */
const char* config_apply_state_to_string(config_apply_state_t state);

#endif /* __CONFIG_APPLY_MANAGER_H__ */
