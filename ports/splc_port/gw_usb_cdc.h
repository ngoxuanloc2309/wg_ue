#ifndef GW_USB_CDC_H
#define GW_USB_CDC_H

/*
 * gw_usb_cdc.h - CDC-ACM only USB device of the gateway (GW_USB_CDC_ENABLE=1).
 *
 * Owner of the USB peripheral in this build: it initialises the controller,
 * runs the TinyUSB device task and publishes g_usb_tusb_initialized (usb.h) so
 * the USB interrupt and the SimplePLC link know when tud_*() is allowed. The
 * only consumer is the SimplePLC App link in splc_port.c (CDC instance 0).
 *
 * It is mutually exclusive with GW_USB_NETIF_ENABLE (usb_netif_task owns USB
 * there). Not available without GW_USB_CDC_ENABLE.
 */

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Create the USB task once. Returns false (and logs why) when called twice or
 * when the task cannot be created. The USB controller is brought up inside the
 * task, so this returns immediately. */
bool gw_usb_cdc_start(void);

#ifdef __cplusplus
}
#endif

#endif /* GW_USB_CDC_H */