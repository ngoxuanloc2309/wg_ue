#ifndef USB_NETIF_H
#define USB_NETIF_H

#if __cplusplus
extern "C" {
#endif

void usb_netif_init();
bool usb_netif_is_available(void);

#if __cplusplus
}
#endif

#endif
