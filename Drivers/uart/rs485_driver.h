/**
 * @file rs485_driver.h
 * @brief RS485 UART Driver Header - STM32H5 HAL with FreeRTOS
 */
#ifndef __RS485_DRIVER_H__
#define __RS485_DRIVER_H__

#include <stdint.h>
#include <stdbool.h>
#include "main.h"
#include "usart.h"
#include "FreeRTOS.h"
#include "stream_buffer.h"
#include "semphr.h"

/* RS485 Port Configuration */
#define RS485_MAX_PORTS    3
#define RS485_RX_BUF_SIZE  512

typedef enum {
    RS485_OWNER_NONE = 0,
    RS485_OWNER_SERIAL_TCP_SERVER,
    RS485_OWNER_SERIAL_TCP_CLIENT,
    RS485_OWNER_MODBUS_MASTER,
    RS485_OWNER_MODBUS_GATEWAY
} rs485_owner_t;

typedef enum {
    RS485_STATE_DISABLED = 0,
    RS485_STATE_STOPPED,
    RS485_STATE_STARTING,
    RS485_STATE_RUNNING,
    RS485_STATE_STOPPING,
    RS485_STATE_RECONFIGURING,
    RS485_STATE_ERROR
} rs485_state_t;

typedef struct {
    uint32_t baudrate;
    uint8_t data_bits;
    uint8_t parity;
    uint8_t stop_bits;
} rs485_runtime_config_t;

typedef struct {
    uint32_t rx_bytes;
    uint32_t tx_bytes;
    uint32_t rx_frames;
    uint32_t tx_frames;
    uint32_t queue_depth;
    uint32_t queue_max_depth;
    uint32_t overflows;
    uint32_t timeouts;
    uint32_t hw_errors; // UART errors, CRC
    uint32_t last_activity_ms;
    uint32_t last_error_code;
} rs485_counters_t;

typedef struct {
    rs485_state_t state;
    rs485_owner_t owner;
    rs485_counters_t counters;
    rs485_runtime_config_t config;
} rs485_port_status_t;

/* Port 0: USART3 - Using CubeMX defines from main.h */
#define RS485_PORT0_DE_PIN    USART3_DE_Pin
#define RS485_PORT0_DE_PORT   USART3_DE_GPIO_Port
#define RS485_PORT0_UART      &huart3

/* Port 1: USART6 - Using CubeMX defines from main.h */
#define RS485_PORT1_DE_PIN    USART6_DE_Pin
#define RS485_PORT1_DE_PORT   USART6_DE_GPIO_Port
#define RS485_PORT1_UART      &huart6

/* Port 2: USART10 - Using CubeMX defines from main.h */
#define RS485_PORT2_DE_PIN    USART10_DE_Pin
#define RS485_PORT2_DE_PORT   USART10_DE_GPIO_Port
#define RS485_PORT2_UART      &huart10

/* RS485 Port Handle */
typedef struct {
    UART_HandleTypeDef *huart;
    GPIO_TypeDef *de_port;
    uint16_t de_pin;
    
    StreamBufferHandle_t rx_stream;
    SemaphoreHandle_t tx_sem;
    SemaphoreHandle_t mutex;
    SemaphoreHandle_t owner_mutex;
    rs485_owner_t owner;
    rs485_state_t state;
    rs485_counters_t counters;
    rs485_runtime_config_t current_cfg;
    
    uint8_t rx_byte; // Temporary buffer for UART IT
} rs485_handle_t;

/* API */
int rs485_port_init(void);
int rs485_port_acquire(uint8_t port, rs485_owner_t owner);
int rs485_port_release(uint8_t port, rs485_owner_t owner);
int rs485_port_reconfigure(uint8_t port, const rs485_runtime_config_t *cfg);
int rs485_port_get_status(uint8_t port, rs485_port_status_t *out);
int rs485_port_wait_tx_idle(uint8_t port, uint32_t timeout_ms);

int32_t rs485_send(uint8_t port, const uint8_t *data, uint16_t len);
int32_t rs485_recv_polling(uint8_t port, uint8_t *data, uint16_t max_len, uint32_t timeout_ms);
void rs485_flush(uint8_t port);

/* DE Pin Control */
void rs485_set_de(uint8_t port, uint8_t state);

/* Interrupt Handlers */
void rs485_uart_rx_irq(uint8_t port);
void rs485_uart_tx_irq(uint8_t port);
void rs485_uart_error_irq(uint8_t port, uint32_t error_code);

#endif /* __RS485_DRIVER_H__ */
