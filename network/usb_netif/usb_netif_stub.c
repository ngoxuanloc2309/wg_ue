/*
 * usb_netif_stub.c - stand-in for usb_netif.c in the CDC-only USB build
 * (GW_USB_CDC_ENABLE=1, see CMakeLists.txt in this directory).
 *
 * gateway.c and http_server.c still call usb_netif_init() and
 * usb_netif_is_available(). With a CDC-only device there is no USB network
 * (no RNDIS/ECM, no DHCP/DNS server), so both do nothing / report "not available".
 */
#include <stdbool.h>

#include "usb_netif.h"

void usb_netif_init(void)
{
}

bool usb_netif_is_available(void)
{
    return false;
}