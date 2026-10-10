#ifndef __SHELL_SERIAL_H__
#define __SHELL_SERIAL_H__

#include <stdint.h>
#include "stm32h5xx_hal.h"

void shell_serial_init(void);
void serial_tx_write(const uint8_t *data, uint16_t length, uint32_t timeout);
void shell_uart_rx_complete(UART_HandleTypeDef *huart);

typedef struct {
    uint32_t rx_irq_count;
    uint32_t rx_byte_count;
    uint32_t rx_queue_drop_count;
} shell_serial_diag_t;

void shell_serial_get_diagnostics(shell_serial_diag_t *diag);

#endif
