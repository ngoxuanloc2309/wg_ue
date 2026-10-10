#ifndef __MODBUS_PARSER_H__
#define __MODBUS_PARSER_H__

#include <stdint.h>
#include <stdbool.h>
#include "cJSON.h"
#include "telemetry_store.h"

#define MAX_MODBUS_DEVICES 10
#define MAX_REGISTERS_PER_DEVICE 10
#define MAX_KEYS_PER_REG 32
#define MODBUS_MAX_PORTS 3

typedef enum {
    MODBUS_PORT_DISABLED = 0,
    MODBUS_PORT_TCP_SERVER,
    MODBUS_PORT_TCP_CLIENT,
    MODBUS_PORT_MASTER,
    MODBUS_PORT_GATEWAY
} modbus_port_mode_t;

typedef struct {
    bool enabled;
    modbus_port_mode_t mode;
    uint32_t baudrate;
    uint8_t data_bits;
    uint8_t parity;
    uint8_t stop_bits;
    uint16_t tcp_port;
    uint8_t max_clients;
} modbus_port_service_config_t;

typedef enum {
    MODBUS_BYTE_ABCD = 0,  // Default - Big-Endian
    MODBUS_BYTE_BADC = 1,  // Big-Endian Mid
    MODBUS_BYTE_CDAB = 2,  // Little-Endian Mid  
    MODBUS_BYTE_DCBA = 3,  // Little-Endian
} modbus_byte_order_t;

/* Register Configuration */
typedef struct {
    uint16_t reg_addr;
    uint16_t count;
    uint8_t func_code; /* 1 (Coils), 2 (Discrete), 3 (Holding), 4 (Input) */
    char data_type[16]; /* "int16", "uint16", "int32", "uint32", "float32", "bool" */
    char keys[MAX_KEYS_PER_REG][32];  /* JSON keys array */
    telemetry_id_t telemetry_ids[MAX_KEYS_PER_REG];
    modbus_byte_order_t byte_order;
    float multiplier;
    float offset;
} modbus_device_reg_t;

/* Device Configuration */
typedef struct {
    uint8_t port;
    uint8_t slave_id;
    char name[32];
    uint32_t poll_interval_ms;
    
    modbus_device_reg_t regs[MAX_REGISTERS_PER_DEVICE];
    uint8_t reg_count;
} modbus_device_t;

/* Global Modbus Configuration */
typedef struct {
    modbus_port_service_config_t ports[MODBUS_MAX_PORTS];
    modbus_device_t devices[MAX_MODBUS_DEVICES];
    uint8_t device_count;
} modbus_config_t;

const modbus_port_service_config_t *modbus_get_port_config(uint8_t port);

extern modbus_config_t g_modbus_config;

/**
 * @brief Parse the Modbus configuration JSON node
 */
void modbus_config_load_cb(cJSON *node);

/**
 * @brief Generate the Modbus configuration JSON node
 */
void modbus_config_save_cb(cJSON *root, bool include_secrets);

/**
 * @brief Parse raw Modbus response into a cJSON object based on device and register block config
 * 
 * @param device_idx Index of the device in g_modbus_config
 * @param reg_idx Index of the register block in the device
 * @param raw_data Raw byte array from Modbus response
 * @param data_len Length of raw_data in bytes
 * @return cJSON* Pointer to the generated JSON object (must be deleted by caller), or NULL on failure
 */
void modbus_parser_process_data(uint8_t port, uint8_t slave_id, uint16_t reg_addr, const uint8_t *raw_data, uint16_t data_len);
void modbus_parser_set_device_timeout(uint8_t port, uint8_t slave_id, uint16_t reg_addr);

void modbus_config_lock(void);
void modbus_config_unlock(void);

#endif /* __MODBUS_PARSER_H__ */

void modbus_config_commit_cb(void);
