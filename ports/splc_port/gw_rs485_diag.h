/*
 * gw_rs485_diag.h - temporary RS485 bring-up diagnostics for ONE host RS485 port.
 *
 * Purpose: check the hardware of a single RS485 port (UART + MAX485 + DE + A/B wiring)
 * with a PC USB-RS485 adapter, independent of SimplePLC and Modbus.
 *
 * Port numbering of the host driver (rs485_driver.h):
 *     0 = USART3 (RX PD9,  DE PD10)
 *     1 = USART6 (RX PC7,  DE PC8)
 *     2 = USART10 (RX PE4, DE PE2)
 *
 * Select the port under test with GW_RS485_DIAG_PORT (default 0 = USART3). It must NOT be
 * a port that SimplePLC uses (SPLC_HOST_RS485_PORT / SPLC_HOST_APP_RS485_PORT);
 * gw_rs485_diag.c stops the build with #error if it is.
 *
 * Usage (ports/splc_port/splc_port.c, SPLC task loop):
 *
 *     gw_rs485_diag_poll(GW_RS485_DIAG_PORT);
 *
 * What it does, all of it logged under the tag RS485_DIAG:
 *   1. Takes ownership of the port when nobody owns it, and sets GW_RS485_DIAG_BAUD 8N1.
 *   2. Every GW_RS485_DIAG_TX_PERIOD_MS sends "hello #<n>\r\n" -> the PC must see it.
 *   3. Anything the PC sends is read, logged as hex and (GW_RS485_DIAG_ECHO = 1) sent
 *      back unchanged -> proves the RX path too.
 *   4. Logs counters (rx/tx bytes, errors, overflows), the DE pin level and the RX pin
 *      level statistics, and a PASS/FAIL line per test.
 *
 * Task context only, never from an ISR. Remove the call when the port works.
 */
#ifndef GW_RS485_DIAG_H
#define GW_RS485_DIAG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Port under test: 0 = USART3, 1 = USART6, 2 = USART10. */
#ifndef GW_RS485_DIAG_PORT
#define GW_RS485_DIAG_PORT          0U
#endif

/*
 * Run the diagnostics of one port. Call it every pass of the SPLC engine task:
 *
 *     gw_rs485_diag_poll(GW_RS485_DIAG_PORT);
 *
 * Only the port equal to GW_RS485_DIAG_PORT is serviced; other values are ignored.
 */
void gw_rs485_diag_poll(uint8_t port);

/*
 * Feed bytes that were read from the port (len == 0 = line is quiet). Used by
 * gw_rs485_diag_poll() and by the --wrap hook in the .c file.
 */
void gw_rs485_diag_rx(const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* GW_RS485_DIAG_H */