#ifndef SPLC_RS485_BUS_H
#define SPLC_RS485_BUS_H

/*
 * splc_rs485_bus.h - RS485 ports of the host as SimplePLC sees them.
 *
 * Two SimplePLC masters talk over the host's RS485 UARTs:
 *   - Remote I/O (plc_io_remote): the I/O boards, on SPLC_HOST_RS485_PORT;
 *   - MB_READ / MB_WRITE (plc_mbm / plc_mbw): sensors and actuators, on up to three
 *     ports, App port 1..3 = SPLC_HOST_MBM_PORT0..2 (splc_port.h).
 *
 * A host UART is either used by one of them alone (no arbitration, nothing to pay) or
 * SHARED (the I/O boards port is also an MB_READ/MB_WRITE port). A shared bus has ONE
 * owner at a time, and the library's masters are never edited:
 *   - the I/O boards go first: an MB_READ/MB_WRITE frame is only put on the wire when
 *     no I/O request is in flight (its send is deferred, the master waits for tx_done());
 *   - an I/O request that finds the bus held by MB_READ/MB_WRITE fails at once (the
 *     Remote I/O master retries by itself; SPLC_REMOTE_OFFLINE_AFTER failures in a row
 *     make the node offline);
 *   - the owner keeps the bus until the answer is complete (silence of ~3.5 characters
 *     after the first byte), or, when no byte comes, SPLC_HOST_BUS_MBM_HOLD_MS after the
 *     frame (I/O boards: the Remote I/O timeout);
 *   - the line settings follow the owner (the App's table 0x1400 for MB_READ/MB_WRITE,
 *     SPLC_HOST_RS485_BAUD/_PARITY for the I/O boards). Giving the shared App port the
 *     same settings as the I/O boards (115200 8N1) avoids the switch, which costs a UART
 *     re-init and one host log line per change.
 *
 * Threading: everything here runs in the SimplePLC engine task.
 */

#include <stdint.h>
#include <stdbool.h>
#include "splc_opt.h"
#include "modbus_serial.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Build the table from the SPLC_HOST_* options. Once, before the registrations below. */
void splc_bus_init(void);

/* Fill `out` for splc_gateway_set_rs485_port() (Remote I/O master). */
void splc_bus_io_port(modbus_serial_port_t *out);

#if SPLC_MBM_ENABLE
/* Register every wired MB_READ/MB_WRITE port with the library
 * (splc_mbm_set_rs485_port). Returns how many were registered. */
unsigned splc_bus_register_mbm(void);
#endif

/* Once per engine loop: release a held shared bus, keep the ports owned and
 * configured (every SPLC_BUS_CHECK_MS). */
void splc_bus_poll(uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* SPLC_RS485_BUS_H */