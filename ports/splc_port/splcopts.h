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
#define SPLC_LOG_LEVEL                4

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

#define SPLC_MBM_ENABLE 1
#endif /* SPLCOPTS_H */