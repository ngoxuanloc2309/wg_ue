/*
 * gw_rs485_diag.h - temporary RS485 bring-up diagnostics (log only).
 */
#ifndef GW_RS485_DIAG_H
#define GW_RS485_DIAG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Log the counters of one host RS485 port (0 = USART3, 1 = USART6, 2 = USART10),
 * the DE/RE pin level and the last frame that arrived ("Rx[n]=hex").
 *
 * Call it every pass of the SPLC engine task, BEFORE plc_engine_poll():
 *
 *     gw_rs485_diag_poll(1U);      // 1 = USART6
 *     plc_engine_poll();
 *
 * By default (GW_RS485_DIAG_DRAIN = 1) it reads the port's receive stream itself
 * to see the bytes, so SimplePLC does NOT receive anything while it is enabled.
 * That is fine for checking the wire, but remove the call afterwards.
 * Task context only, never from an ISR.
 */
void gw_rs485_diag_poll(uint8_t port);

/*
 * Feed bytes that were read from the port (len == 0 = line is quiet). Used by
 * gw_rs485_diag_poll() in drain mode, or by the --wrap hook in the .c file.
 */
void gw_rs485_diag_rx(const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* GW_RS485_DIAG_H */