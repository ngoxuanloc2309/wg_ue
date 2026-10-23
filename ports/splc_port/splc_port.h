#ifndef SPLC_PORT_H
#define SPLC_PORT_H

/*
 * splc_port.h - glue between the GatewayV2 host firmware and SimplePLC.
 *
 * SimplePLC is embedded the way lwIP is: the host supplies a small port file
 * (this one) and an options file (splcopts.h next to it). Nothing inside
 * SimplePLC is edited, and none of the host's drivers are edited either.
 *
 * What this port plugs in:
 *   - RS485 master port to the I/O boards : splc_gateway_set_rs485_port()
 *       built on the host driver Drivers/uart/rs485_driver.c (owner
 *       RS485_OWNER_MODBUS_GATEWAY, so modbus_master / serial_tcp_server
 *       cannot take the same port: their rs485_port_acquire() fails).
 *   - App <-> device byte pipe            : splc_link_register(), ONE of:
 *       a) RS485 (SPLC_HOST_APP_RS485_PORT >= 0): a second host RS485 port, the
 *          device is a Modbus RTU SLAVE for the App (PC + USB-RS485 dongle).
 *          Two functions on top of rs485_send()/rs485_recv_polling(): output()
 *          and the pump that feeds splc_link_input(). No USB involved.
 *       b) USB CDC (SPLC_HOST_APP_RS485_PORT < 0, default) built on TinyUSB
 *          CDC instance 0. Two ways to get that CDC:
 *         - GW_USB_NETIF_ENABLE=1: the CDC-ACM of the composite descriptor;
 *           it exists only after usb_netif_task ran tusb_init(), i.e. after
 *           WiFi got an IP.
 *         - GW_USB_CDC_ENABLE=1: a CDC-only device owned by this port
 *           (gw_usb_cdc.c), started by splc_host_start(), no WiFi needed.
 *   - MB_READ / MB_WRITE (SPLC_MBM_ENABLE / SPLC_MBW_ENABLE): up to 3 RS485 ports, App
 *       port 1..3 = SPLC_HOST_MBM_PORT0..2 below. App port 1 is USART6, the I/O boards'
 *       UART: both masters share it through the arbiter of splc_rs485_bus.c.
 *   - MQTT Subscribe (SPLC_MQTT_SUB_ENABLE): splc_mqtt_port.c subscribes what the App
 *       configured (apps/mqtt_client) and feeds the messages to the library.
 *   - Wi-Fi settings from the App (SPLC_WIFI_ENABLE): splc_wifi_port.c on top of
 *       network_manager / the ST67 driver, with a small worker task.
 *   - One FreeRTOS task that calls plc_engine_init() and then plc_engine_poll().
 *       SimplePLC is single-threaded: no other task may touch its tags/rules.
 *
 * Call splc_host_start() ONCE from gateway.c, AFTER rs485_port_init() and
 * modbus_master_init() (the RS485 port only becomes RUNNING in
 * rs485_port_reconfigure(); this port also retries by itself every second).
 */

#include <stdbool.h>
#include "splc_opt.h"        /* pulls in splcopts.h of this product */

/* ---- Options of THIS port (override in splcopts.h) ---------------------- */

/* RS485 port used for the I/O boards: 0 = USART3, 1 = USART6, 2 = USART10
 * (index of rs485_driver.h). Must not be configured for any host service. */
#ifndef SPLC_HOST_RS485_PORT
#define SPLC_HOST_RS485_PORT      1
#endif

/* App link over RS485: index of the host RS485 port (0 = USART3, 1 = USART6,
 * 2 = USART10) the App (PC + USB-RS485 dongle) is wired to. Negative (default)
 * = the App link is the USB CDC. Must be a DIFFERENT port than
 * SPLC_HOST_RS485_PORT (that one is the master bus to the I/O boards) and must
 * not be configured for any host service (the port is taken as
 * RS485_OWNER_MODBUS_GATEWAY, like the I/O port). The App's Modbus RTU unit id
 * is SPLC_MODBUS_UNIT_ID. */
#ifndef SPLC_HOST_APP_RS485_PORT
#define SPLC_HOST_APP_RS485_PORT  (-1)
#endif
#ifndef SPLC_HOST_APP_RS485_BAUD
#define SPLC_HOST_APP_RS485_BAUD  115200U   /* the PC side must use the same, 8N1 */
#endif
#ifndef SPLC_HOST_APP_RS485_PARITY
#define SPLC_HOST_APP_RS485_PARITY 0
#endif

/* Derived (do not define): 1 when the App link is the RS485 port above. */
#define SPLC_HOST_APP_LINK_RS485  (SPLC_HOST_APP_RS485_PORT >= 0)

/* Line settings of the I/O boards' RS485 bus. parity: 0 none, 1 odd, 2 even
 * (rs485_driver.c). Data bits 8, stop bits 1. */
#ifndef SPLC_HOST_RS485_BAUD
#define SPLC_HOST_RS485_BAUD      115200U
#endif
#ifndef SPLC_HOST_RS485_PARITY
#define SPLC_HOST_RS485_PARITY    0
#endif

/* MB_READ / MB_WRITE ports. App port N (1..3) is index N-1; the value is the host RS485
 * index (0 = USART3, 1 = USART6, 2 = USART10) or -1 when that App port is not wired.
 * Default = how the enclosure is labelled: port 1 = USART6, 2 = USART10, 3 = USART3.
 * SPLC_MBM_PORT_COUNT (splcopts.h) is 1, 2 or 4 and must cover the wired ports: three
 * wired ports need 4 (App port 4 stays unregistered: its blocks report err_code 1). */
#ifndef SPLC_HOST_MBM_PORT0
#define SPLC_HOST_MBM_PORT0       1
#endif
#ifndef SPLC_HOST_MBM_PORT1
#define SPLC_HOST_MBM_PORT1       2
#endif
#ifndef SPLC_HOST_MBM_PORT2
#define SPLC_HOST_MBM_PORT2       0
#endif

/* 1 = an App port on the same UART as SPLC_HOST_RS485_PORT (the I/O boards) is shared with
 * the Remote I/O master (arbiter in splc_rs485_bus.c: I/O boards first). 0 = such an App
 * port is not registered. Sharing costs the I/O boards: a sensor answer still on the wire
 * makes their request fail (node offline after SPLC_REMOTE_OFFLINE_AFTER failures in a
 * row), so keep reads there short, or give MB_READ its own port. */
#ifndef SPLC_HOST_MBM_SHARE_IO_BUS
#define SPLC_HOST_MBM_SHARE_IO_BUS 1
#endif

/* How long (ms) a shared bus stays held by MB_READ/MB_WRITE after its frame while NO byte
 * of the answer has come. Longer = more patience for slow slaves, more outage for the I/O
 * boards when a slave is absent. */
#ifndef SPLC_HOST_BUS_MBM_HOLD_MS
#define SPLC_HOST_BUS_MBM_HOLD_MS 150U
#endif

/* Engine task. Stack is in WORDS (4 bytes). Priority must stay above
 * usb_netif_task (5) so the scan keeps its 10 ms period, and below the
 * application band (10..12) so a Flash erase or a log burst cannot starve it. */
#ifndef SPLC_HOST_TASK_STACK_WORDS
#define SPLC_HOST_TASK_STACK_WORDS 1536U
#endif
#ifndef SPLC_HOST_TASK_PRIO
#define SPLC_HOST_TASK_PRIO        6U
#endif

#if (SPLC_HOST_RS485_PORT < 0) || (SPLC_HOST_RS485_PORT > 2)
#error "SPLC_HOST_RS485_PORT must be 0 (USART3), 1 (USART6) or 2 (USART10)"
#endif
#if (SPLC_HOST_RS485_PARITY < 0) || (SPLC_HOST_RS485_PARITY > 2)
#error "SPLC_HOST_RS485_PARITY must be 0, 1 or 2"
#endif
#if SPLC_HOST_APP_RS485_PORT > 2
#error "SPLC_HOST_APP_RS485_PORT must be -1 (USB CDC) or 0 (USART3), 1 (USART6), 2 (USART10)"
#endif
#if SPLC_HOST_APP_LINK_RS485 && (SPLC_HOST_APP_RS485_PORT == SPLC_HOST_RS485_PORT)
#error "SPLC_HOST_APP_RS485_PORT is the same port as SPLC_HOST_RS485_PORT (I/O boards bus): the App link needs its own RS485 port"
#endif
#if (SPLC_HOST_APP_RS485_PARITY < 0) || (SPLC_HOST_APP_RS485_PARITY > 2)
#error "SPLC_HOST_APP_RS485_PARITY must be 0, 1 or 2"
#endif
#if SPLC_HOST_APP_LINK_RS485 && GW_USB_CDC_ENABLE
#error "App link is RS485 (SPLC_HOST_APP_RS485_PORT >= 0) but GW_USB_CDC_ENABLE=ON starts a USB CDC nobody uses: configure with -DGW_USB_CDC_ENABLE=OFF"
#endif

#if (SPLC_HOST_MBM_PORT0 < -1) || (SPLC_HOST_MBM_PORT0 > 2) || \
    (SPLC_HOST_MBM_PORT1 < -1) || (SPLC_HOST_MBM_PORT1 > 2) || \
    (SPLC_HOST_MBM_PORT2 < -1) || (SPLC_HOST_MBM_PORT2 > 2)
#error "SPLC_HOST_MBM_PORT0..2 must be -1 (not wired), 0 (USART3), 1 (USART6) or 2 (USART10)"
#endif
#if SPLC_MBM_ENABLE
#if (SPLC_HOST_MBM_PORT0 >= 0 && (SPLC_HOST_MBM_PORT0 == SPLC_HOST_MBM_PORT1 || SPLC_HOST_MBM_PORT0 == SPLC_HOST_MBM_PORT2)) || \
    (SPLC_HOST_MBM_PORT1 >= 0 && SPLC_HOST_MBM_PORT1 == SPLC_HOST_MBM_PORT2)
#error "SPLC_HOST_MBM_PORT0..2: two App ports on the same host UART"
#endif
#if SPLC_HOST_APP_LINK_RS485 && \
    (SPLC_HOST_MBM_PORT0 == SPLC_HOST_APP_RS485_PORT || SPLC_HOST_MBM_PORT1 == SPLC_HOST_APP_RS485_PORT || \
     SPLC_HOST_MBM_PORT2 == SPLC_HOST_APP_RS485_PORT)
#error "an MB_READ/MB_WRITE port is the RS485 port of the App link (SPLC_HOST_APP_RS485_PORT)"
#endif
#if (SPLC_HOST_MBM_PORT2 >= 0 && SPLC_MBM_PORT_COUNT < 3) || (SPLC_HOST_MBM_PORT1 >= 0 && SPLC_MBM_PORT_COUNT < 2)
#error "SPLC_MBM_PORT_COUNT (splcopts.h) is smaller than the number of wired App ports (3 wired ports need 4)"
#endif
#endif /* SPLC_MBM_ENABLE */

#ifdef __cplusplus
extern "C" {
#endif

/* Register the RS485 port and the App link, then create the engine task.
 * Returns false (and logs why) when called twice, when registration fails or
 * when the task cannot be created. */
bool splc_host_start(void);

#ifdef __cplusplus
}
#endif

#endif /* SPLC_PORT_H */