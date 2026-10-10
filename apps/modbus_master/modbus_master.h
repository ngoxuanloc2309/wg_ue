/**
 * @file modbus_master.h
 * @brief Modbus RTU Master - Multi-Port Support
 */
#ifndef __MODBUS_MASTER_H__
#define __MODBUS_MASTER_H__

#include <stdint.h>
#include <stdbool.h>

#define MODBUS_MAX_PORTS    3
#define MODBUS_MAX_POINTS_PER_PORT 10
#define MODBUS_MAX_WORDS_PER_POLL  64   /* Max registers/coils per single read (Metter uses 64) */
#define MODBUS_QUEUE_SIZE          32   /* Power of 2 queue size */

typedef enum {
    MB_FUNC_READ_COILS = 0x01,
    MB_FUNC_READ_DISCRETE_INPUTS = 0x02,
    MB_FUNC_READ_HOLDING_REGISTERS = 0x03,
    MB_FUNC_READ_INPUT_REGISTERS = 0x04,
    MB_FUNC_WRITE_SINGLE_COIL = 0x05,
    MB_FUNC_WRITE_SINGLE_REGISTER = 0x06,
    MB_FUNC_WRITE_MULTIPLE_COILS = 0x0F,   /* 15 */
    MB_FUNC_WRITE_MULTIPLE_REGISTERS = 0x10 /* 16 */
} modbus_func_code_t;

typedef struct {
    uint8_t slave_id;
    modbus_func_code_t func_code;
    uint16_t reg_addr;
    uint16_t quantity;
    uint32_t poll_interval_ms;
    uint32_t last_poll_tick;
} modbus_point_t;

typedef struct {
    uint32_t baudrate;
    uint8_t parity;
    modbus_point_t points[MODBUS_MAX_POINTS_PER_PORT];
    int num_points;
} modbus_port_config_t;

typedef struct {
    uint8_t port;
    uint8_t slave_id;
    modbus_func_code_t func_code;
    uint16_t reg_addr;
    uint16_t quantity;
    uint32_t timestamp; /* Timestamp in ticks when data was read */
    uint16_t data[MODBUS_MAX_WORDS_PER_POLL];
} modbus_telemetry_msg_t;

bool modbus_queue_receive(modbus_telemetry_msg_t *msg);

/* Default Baudrates */
#define MODBUS_DEFAULT_BAUD     9600
#define MODBUS_DEFAULT_PARITY  0   /* 0 = None, 1 = Odd */

/* Task Configuration */
#define MODBUS_TASK_STACK      2048
#define MODBUS_TASK_PRIO       24

void modbus_master_init(void);

/**
 * @brief Modbus Master task - one per port
 * @param params: port number (uint8_t cast to void*)
 */
void modbus_task(void *params);

/**
 * @brief Public API for reading holding registers
 * @param port: RS485 port (0-2)
 * @param addr: Modbus slave address (1-247)
 * @param start: Start register address
 * @param count: Number of registers to read
 * @param data: Destination buffer
 * @return 0 on success, -1 on failure
 */
int modbus_read_holding(uint8_t port, uint8_t addr, uint16_t start, uint16_t count, uint16_t *data);

/**
 * @brief Public API for reading input registers
 * @param port: RS485 port (0-2)
 * @param addr: Modbus slave address (1-247)
 * @param start: Start register address
 * @param count: Number of registers to read
 * @param data: Destination buffer
 * @return 0 on success, -1 on failure
 */
int modbus_read_input(uint8_t port, uint8_t addr, uint16_t start, uint16_t count, uint16_t *data);

/**
 * @brief Public API for writing single register
 * @param port: RS485 port (0-2)
 * @param addr: Modbus slave address (1-247)
 * @param reg: Register address
 * @param value: Value to write
 * @return 0 on success, -1 on failure
 */
int modbus_write_single(uint8_t port, uint8_t addr, uint16_t reg, uint16_t value);

/**
 * @brief Public API for writing multiple registers
 * @param port: RS485 port (0-2)
 * @param addr: Modbus slave address (1-247)
 * @param start: Start register address
 * @param count: Number of registers
 * @param data: Source buffer
 * @return 0 on success, -1 on failure
 */
int modbus_write_multiple(uint8_t port, uint8_t addr, uint16_t start, uint16_t count, const uint16_t *data);

/**
 * @brief Public API for writing single coil
 * @param port: RS485 port (0-2)
 * @param addr: Modbus slave address (1-247)
 * @param coil: Coil address
 * @param value: true (ON) or false (OFF)
 * @return 0 on success, -1 on failure
 */
int modbus_write_single_coil(uint8_t port, uint8_t addr, uint16_t coil, bool value);

/**
 * @brief Public API for writing multiple coils
 * @param port: RS485 port (0-2)
 * @param addr: Modbus slave address (1-247)
 * @param start: Start coil address
 * @param count: Number of coils
 * @param data: Source buffer (bits packed in bytes)
 * @return 0 on success, -1 on failure
 */
int modbus_write_multiple_coils(uint8_t port, uint8_t addr, uint16_t start, uint16_t count, const uint8_t *data);

#endif /* __MODBUS_MASTER_H__ */
