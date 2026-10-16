#ifndef GW_USB_CDC_H
#define GW_USB_CDC_H

/*
 * gw_usb_cdc.h - CDC-ACM only USB device of the gateway (GW_USB_CDC_ENABLE=1).
 *
 * This is the gateway twin of SimplePLC's own USB module
 * (libs/simple_plc/simple_plc/port/usb/splc_usb_cdc.*), which is proven on the
 * Zigbee-IO board. It follows the same shape:
 *
 *   - ONE module owns the USB peripheral AND the byte pipe. Nothing else in the
 *     host calls tud_*() while GW_USB_CDC_ENABLE=1.
 *   - bring-up order is the Zigbee-IO one: HAL_PCD_Init() (clock source, VDDUSB,
 *     peripheral reset) -> tusb_init(FULL speed) -> TinyUSB runs.
 *   - the pipe is the two functions SimplePLC's modbus_link asks a port for.
 *     The core (modbus_link) keeps the RX ring and EVERY timeout; this module
 *     only moves bytes.
 *
 * Differences that are forced by the host (not by taste):
 *   - TinyUSB runs with CFG_TUSB_OS = FreeRTOS (ports/usb_port/Target/tusb_config.h),
 *     so tud_task_ext() runs in its own task and the SimplePLC engine task may
 *     use the CDC FIFOs through TinyUSB's OS mutex.
 *   - USB_DRD_FS_IRQHandler() lives in Core/Src/stm32h5xx_it.c (CubeMX file, not
 *     edited): it calls tud_int_handler() once g_usb_tusb_initialized is set.
 *
 * Mutually exclusive with GW_USB_NETIF_ENABLE (usb_netif_task owns USB there).
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Create the USB task once. Returns false (and logs why) when called twice or
 * when the task cannot be created. The USB controller is brought up inside the
 * task, so this returns immediately. */
bool gw_usb_cdc_start(void);

/* true once tusb_init() succeeded. */
bool gw_usb_cdc_ready(void);

/* true when the PC enumerated the device AND opened the COM port (DTR high). */
bool gw_usb_cdc_connected(void);

/* modbus_link process() adapter: drain TinyUSB's CDC RX FIFO into
 * splc_link_input(). Waits for nothing. Engine task only. */
void gw_usb_cdc_process(void *ctx);

/* modbus_link output() adapter: hand up to len bytes to TinyUSB WITHOUT
 * blocking and return how many it took (0 = FIFO full, the core retries);
 * < 0 = no host / port closed, the core aborts the write. Engine task only. */
int32_t gw_usb_cdc_output(const uint8_t *buf, uint16_t len, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* GW_USB_CDC_H */