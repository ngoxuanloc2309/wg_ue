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

/* The host owns USB (usb_netif.c: TinyUSB + descriptors + ISR). */
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
#define SPLC_HOST_RS485_PORT          2          /* USART3: I/O boards bus */
#define SPLC_HOST_RS485_BAUD          115200U    /* ASSUMED: I/O board baud, to be confirmed */

/* App link (SimplePLC.Studio / test scripts) over RS485 instead of USB: the PC
 * reaches the gateway through a USB-RS485 dongle wired to this host RS485 port
 * (0 = USART3, 1 = USART6, 2 = USART10). It must NOT be the I/O boards port above
 * and must not be given to a host service (modbus_master / serial_tcp_server).
 * USART6 = index 1, chosen by the user for the rule-upload test.
 * Remove the line (or set -1) to get the USB CDC App link back; with RS485 the
 * firmware must be configured with -DGW_USB_CDC_ENABLE=OFF. */
/* USB CDC App link (GW_USB_CDC_ENABLE=ON): RS485 App link disabled.
 * Re-enable the two lines below (and build with -DGW_USB_CDC_ENABLE=OFF) to go back to RS485. */
#define SPLC_HOST_APP_RS485_PORT   1    /* USART6 */
#define SPLC_HOST_APP_RS485_BAUD   115200U 

#endif /* SPLCOPTS_H */