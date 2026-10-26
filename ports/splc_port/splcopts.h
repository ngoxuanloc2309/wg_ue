#ifndef SPLCOPTS_H
#define SPLCOPTS_H

/*
 * splcopts.h - SimplePLC configuration of the GatewayV2 firmware (STM32H563).
 * Library defaults and the full option list: simple_plc/config/splc_opt.h.
 * Only options that differ from the default (or worth stating) are listed.
 */

/* Target */
#define SPLC_PLATFORM                 SPLC_PLATFORM_STM32H5
#define SPLC_BOARD                    SPLC_BOARD_GATEWAY

/* STM32H563 = 2 MB Flash. The 40 KB data hole (Rule A/B + Retain) cannot be
 * the last 5 sectors here: the host already uses the whole Flash (bootloader,
 * app slots, scratch). SPLC_FLASH_DATA_ADDR places it explicitly.
 * !!! PROPOSED address, NOT yet checked against bootloader/flash_define.h and
 * !!! the linker script. Do not flash a build with it before that is done. */
#define SPLC_FLASH_SIZE_KB            2048
#define SPLC_FLASH_DATA_ADDR          0x081F0000UL

/* The host runs FreeRTOS; splc_port.c creates the single engine task. */
#define SX_OS_USE_FREERTOS            1

/* The host owns USB (ports/splc_port/gw_usb_cdc.c: TinyUSB CDC-ACM; usb_netif.c in
 * the GW_USB_NETIF_ENABLE build), not SimplePLC's own port/usb. */
#define SPLC_PORT_USB_CDC             0

/* Log through the host's libs/logger (LOGx macros / log_func). */
#define SPLC_LOGGER_BUILTIN           0
#define SPLC_LOG_LEVEL                3

/* The host firmware has no CubeMX RTC or ADC (no hrtc / hadc in GatewayV2), so
 * SimplePLC's STM32H5 drivers for them must not be built. splc_port.c supplies
 * the sx_rtc_*() stubs ("no time source yet"); the gateway has no analog input. */
#define SPLC_RTC_BUILTIN              0
#define SPLC_ADC_BUILTIN              0

/* The host has no PVD handler and nothing is known about a hold-up circuit
 * on the gateway: keep the emergency retain save off. */
#define PLC_PVD_EMERGENCY_SAVE_ENABLE 0

/* MQTT Publish (docs/plc_mqtt_design.md): SimplePLC decides when to publish and
 * renders the payload; ports/splc_port/splc_mqtt_port.c hands it to apps/mqtt_client.
 * TLS stays unsupported (the bundled lwIP client has none). */
#define SPLC_MQTT_ENABLE              1

/* ---- options of ports/splc_port (defaults in splc_port.h) ---------------- */

/* Master bus to the I/O boards: host RS485 port 1 = USART6 (RX PC7, DE PC8).
 * Port numbering of Drivers/uart/rs485_driver.h: 0 = USART3, 1 = USART6, 2 = USART10. */
#define SPLC_HOST_RS485_PORT          1
#define SPLC_HOST_RS485_BAUD          115200U    /* ASSUMED: I/O board baud, to be confirmed */

/* App link (SimplePLC.Studio) = USB CDC (build with GW_USB_CDC_ENABLE=ON): do not define
 * SPLC_HOST_APP_RS485_PORT (default -1). To carry the App over a second RS485 port
 * instead, define SPLC_HOST_APP_RS485_PORT / SPLC_HOST_APP_RS485_BAUD to a port that is
 * not SPLC_HOST_RS485_PORT, and build with -DGW_USB_CDC_ENABLE=OFF. */

/* MB_READ / MB_WRITE: three RS485 ports (App port 1 = USART6, 2 = USART10, 3 = USART3; see
 * SPLC_HOST_MBM_PORT0..2 in splc_port.h). The Studio sees SPLC_MBM_PORT_COUNT ports (0x0A0A).
 * App port 1 is the I/O boards' UART and is shared with Remote I/O through the arbiter in
 * splc_rs485_bus.c. */
#define SPLC_MBM_ENABLE        1
#define SPLC_MBM_PORT_COUNT    3
#define SPLC_MBW_ENABLE        1

/* MQTT Subscribe: ports/splc_port/splc_mqtt_port.c + apps/mqtt_client. */
#define SPLC_MQTT_SUB_ENABLE   1

/* Wi-Fi settings from the App: ports/splc_port/splc_wifi_port.c. */
#define SPLC_WIFI_ENABLE       1

/* App link over LAN (Modbus TCP) - implemented in ports/splc_port/splc_modbus_tcp_port.c.
 * ON: the App (or a test script) can also talk to the gateway over the network, next to the
 * existing link. It listens on SPLC_HOST_MODBUS_TCP_PORT (apps/modbus_tcp_server owns 502, so
 * the default is 5020), costs about 392 B RAM per extra transport (only ~3 KB of static RAM
 * are left, handoff decision 26: check the link still passes) and has NO authentication:
 * limit the clients with SPLC_HOST_MODBUS_TCP_ALLOW_NET/_MASK (e.g. 192.168.1.0 / 255.255.255.0)
 * or turn it off again after the test.
 * OFF (previous state): MAX_TRANSPORTS 1 and TCP_ENABLE 0 give a binary identical to a build
 * without the TCP code. */
#define SPLC_MODBUS_MAX_TRANSPORTS      2
#define SPLC_HOST_MODBUS_TCP_ENABLE     1
#define SPLC_HOST_MODBUS_TCP_PORT       5020      /* 1..65535, must not be 502 */
/* #define SPLC_HOST_MODBUS_TCP_ALLOW_NET   0xC0A80100UL   192.168.1.0  (0 = any client) */
/* #define SPLC_HOST_MODBUS_TCP_ALLOW_MASK  0xFFFFFF00UL   255.255.255.0 */
#endif /* SPLCOPTS_H */