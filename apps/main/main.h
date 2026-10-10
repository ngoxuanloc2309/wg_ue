/**
 * @file main.h
 * @brief GatewayV2 - Entry Point Header
 */
#ifndef __MAIN_H__
#define __MAIN_H__

#include "stm32h5xx_hal.h"
#include <stdint.h>

/* Log putchar for logger */
void log_puts(const char *str);

/* Shared console TX path.  All task-level console output must use this
 * function so concurrent logger/shell/vendor writes remain line-safe. */
void serial_tx_write(const uint8_t *data, uint16_t length, uint32_t timeout);

#endif /* __MAIN_H__ */
