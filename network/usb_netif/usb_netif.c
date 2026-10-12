/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2020 Peter Lawrence
 *
 * influenced by lrndis https://github.com/fetisov/lrndis
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

/*
this appears as either a RNDIS or CDC-ECM USB virtual network adapter; the OS picks its preference

RNDIS should be valid on Linux and Windows hosts, and CDC-ECM should be valid on Linux and macOS hosts

The MCU appears to the host as IP address 192.168.7.1, and provides a DHCP server, DNS server, and web server.

Link State Control:
- Press the user button to toggle the network link state (UP/DOWN)
- This simulates "ethernet cable unplugged/plugged" events
- The host OS will see the network interface as disconnected/connected accordingly
- Use this to test network error handling and recovery in host applications
*/
/*
Some smartphones *may* work with this implementation as well, but likely have limited (broken) drivers,
and likely their manufacturer has not tested such functionality.  Some code workarounds could be tried:

The smartphone may only have an ECM driver, but refuse to automatically pick ECM (unlike the OSes above);
try modifying ./examples/devices/net_lwip_webserver/usb_descriptors.c so that CONFIG_ID_ECM is default.

The smartphone may be artificially picky about which Ethernet MAC address to recognize; if this happens,
try changing the first byte of tud_network_mac_address[] below from 0x02 to 0x00 (clearing bit 1).
*/

/*
 * GW_USB_CDC_ENABLE = 1 (CDC-ACM only USB device, see ports/splc_port/gw_usb_cdc.c):
 * there is no USB network class, and TinyUSB is owned by the gw_usb_cdc task.
 * Only the two public entry points remain, as stubs, because wifi_driver.c,
 * http_server.c and gateway.c include usb_netif.h unconditionally. Everything
 * else (lwIP netif, DHCP/DNS, tud_network_*_cb, tud_*_cb, usb_netif_task) is
 * compiled out: with CFG_TUD_ECM_RNDIS = CFG_TUD_NCM = 0 those APIs do not exist.
 */
#if !GW_USB_CDC_ENABLE
#include "main.h"
#include "usb.h"
#include "tusb.h"

#include "dhserver.h"
#include "dnserver.h"
#include "lwip/ethip6.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/timeouts.h"
#include "lwip/sys.h"
#include "tusb_config.h"
#include "FreeRTOS.h"
#include "task.h"
#include "lwip/tcpip.h"
#include "logger.h"
#include "network_manager.h"
#include "app_config.h"


static const char *TAG = "usb-netif";
static struct netif netif_data;
static volatile bool s_usb_available = false;
static volatile bool s_usb_services_started = false;
static volatile bool s_usb_link_request = false;
static volatile bool s_usb_netif_initialized = false;
static const dhcp_config_t dhcp_config;
static bool dns_query_proc(const char *name, ip4_addr_t *addr);
bool usb_netif_is_available(void);

static void usb_netif_services_work(void *arg) {
  (void)arg;
  if (!s_usb_link_request || !netif_is_up(&netif_data)) {
    dhserv_free();
    dnserv_free();
    s_usb_services_started = false;
    s_usb_available = false;
    return;
  }
  if (dhserv_init(&dhcp_config) != ERR_OK ||
      dnserv_init(IP_ADDR_ANY, 53, dns_query_proc) != ERR_OK) {
    dhserv_free();
    dnserv_free();
    s_usb_services_started = false;
    s_usb_available = false;
    LOGW(TAG, "USB DHCP/DNS service start failed");
    return;
  }
  s_usb_services_started = true;
  s_usb_available = true;
  LOGI(TAG, "USB DHCP/DNS services ready");
}

static void usb_netif_link_work(void *arg) {
  (void)arg;
  if (s_usb_link_request) {
    netif_set_link_up(&netif_data);
    usb_netif_services_work(NULL);
  } else {
    netif_set_link_down(&netif_data);
    s_usb_available = false;
    s_usb_services_started = false;
    dhserv_free();
    dnserv_free();
  }
}
#define INIT_IP4(a, b, c, d) \
  { PP_HTONL(LWIP_MAKEU32(a, b, c, d)) }
#define RNDIS_HDR_LEN 44
#define USB_NETIF_XMIT_RETRIES 100U
#define USB_NETIF_TASK_PRIORITY 5U
#define USB_NETIF_LWIP_INIT_TIMEOUT_MS 5000U
#define USB_NETIF_MOUNT_TIMEOUT_MS 30000U
/* Blink pattern
 * - 250 ms  : device not mounted
 * - 1000 ms : device mounted
 * - 2500 ms : device is suspended
 */
enum {
  BLINK_NOT_MOUNTED = 250,
  BLINK_MOUNTED = 1000,
  BLINK_SUSPENDED = 2500,
};

static uint32_t blink_interval_ms = BLINK_NOT_MOUNTED;


/* lwip context */
/* this is used by this code, ./class/net/net_driver.c, and usb_descriptors.c */
/* ideally speaking, this should be generated from the hardware's unique ID (if available) */
/* it is suggested that the first byte is 0x02 to indicate a link-local address */
uint8_t tud_network_mac_address[6] = {0x02, 0x02, 0x84, 0x6A, 0x96, 0x00};

/* network parameters of this MCU */
static const ip4_addr_t ipaddr = INIT_IP4(192, 168, 7, 1);
static const ip4_addr_t netmask = INIT_IP4(255, 255, 255, 0);
static const ip4_addr_t gateway = INIT_IP4(0, 0, 0, 0);

/* database IP addresses that can be offered to the host; this must be in RAM to store assigned MAC addresses */
static dhcp_entry_t entries[] = {
    /* mac ip address               lease time */
    {{0}, INIT_IP4(192, 168, 7, 2), 24 * 60 * 60},
    {{0}, INIT_IP4(192, 168, 7, 3), 24 * 60 * 60},
    {{0}, INIT_IP4(192, 168, 7, 4), 24 * 60 * 60},
};

static const dhcp_config_t dhcp_config = {
    .router = INIT_IP4(0, 0, 0, 0),  /* router address (if any) */
    .port = 67,                      /* listen port */
    .dns = INIT_IP4(192, 168, 7, 1), /* dns server (if any) */
    "usb",                           /* dns suffix */
    TU_ARRAY_SIZE(entries),          /* num entry */
    entries                          /* entries */
};

static err_t linkoutput_fn(struct netif *netif, struct pbuf *p) {
  (void) netif;

  if (p == NULL || p->tot_len == 0U) {
    return ERR_ARG;
  }

  for (uint32_t attempt = 0U; attempt < USB_NETIF_XMIT_RETRIES; ++attempt) {
    /* if TinyUSB isn't ready, we must signal back to lwip that there is nothing we can do */
    if (!tud_ready())
      return ERR_IF;

    /* if the network driver can accept another packet, we make it happen */
    if (tud_network_can_xmit(p->tot_len)) {
      tud_network_xmit(p, 0 /* unused for this example */);
      return ERR_OK;
    }

    /* transfer execution to TinyUSB in the hopes that it will finish transmitting the prior packet */
    /* With CFG_TUSB_OS=FreeRTOS, tud_task() waits indefinitely for an
     * event. Use the bounded form so diagnostics and hot-plug state polling
     * continue even when the host is disconnected or electrically silent. */
    tud_task_ext(5U, false);
  }

  LOGW(TAG, "USB TX unavailable after %u retries; dropping packet",
       (unsigned)USB_NETIF_XMIT_RETRIES);
  return ERR_BUF;
}

static err_t ip4_output_fn(struct netif *netif, struct pbuf *p, const ip4_addr_t *addr) {
  return etharp_output(netif, p, addr);
}

#if LWIP_IPV6
static err_t ip6_output_fn(struct netif *netif, struct pbuf *p, const ip6_addr_t *addr) {
  return ethip6_output(netif, p, addr);
}
#endif

static err_t netif_init_cb(struct netif *netif) {
  LWIP_ASSERT("netif != NULL", (netif != NULL));
  netif->mtu = CFG_TUD_NET_MTU;
  netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP | NETIF_FLAG_UP;
  netif->state = NULL;
  netif->name[0] = 'E';
  netif->name[1] = 'X';
  netif->linkoutput = linkoutput_fn;
  netif->output = ip4_output_fn;
LOGD(TAG,"output=%p", netif->output);
LOGD(TAG,"linkoutput=%p", netif->linkoutput);
#if LWIP_IPV6
  netif->output_ip6 = ip6_output_fn;
#endif
  return ERR_OK;
}

/* notifies the USB host about the link state change. */
static void usbnet_netif_link_callback(struct netif *netif) {
    bool link_up = netif_is_link_up(netif);
    s_usb_available = link_up && netif_is_up(netif);
    tud_network_link_state(BOARD_TUD_RHPORT, link_up);
    if(link_up){
      LOGD(TAG,"USB netif is link up");
    }
    else{
      LOGD(TAG,"USB netif is link down");
    }
}
void ip_status_callback(struct netif *netif){
  if(netif_is_up(netif)){
    LOGD(TAG,"Netif is up");
    LOGD(TAG, "IP : %s", ipaddr_ntoa(&netif->ip_addr));
    LOGD(TAG, "MASK : %s", ipaddr_ntoa(&netif->netmask));
    LOGD(TAG, "GW : %s", ipaddr_ntoa(&netif->gw));
  }
  else{
    LOGD(TAG,"Netif is down");
  }
}
static void init_usb_netif_cb(void *ctx) {
  (void)ctx;
  struct netif *netif = &netif_data;

  /* the lwip virtual MAC address must be different from the host's; to ensure this, we toggle the LSbit */
  netif->hwaddr_len = sizeof(tud_network_mac_address);
  memcpy(netif->hwaddr, tud_network_mac_address, sizeof(tud_network_mac_address));
  netif->hwaddr[5] ^= 0x01;
  LOGD(TAG,"MAC Address : %02X:%02X:%02X:%02X:%02X:%02X",netif->hwaddr[0],netif->hwaddr[1]
                                                      ,netif->hwaddr[2],netif->hwaddr[3]
                                                      ,netif->hwaddr[4],netif->hwaddr[5]);
  netif = netif_add(netif, &ipaddr, &netmask, &gateway, NULL, netif_init_cb, tcpip_input);
#if LWIP_IPV6
  netif_create_ip6_linklocal_address(netif, 1);
#endif
  /* NOTE: USB is for local config only (192.168.7.x), not Internet access.
   * DO NOT call netif_set_default() here - network_manager handles default gateway. */
#if LWIP_NETIF_LINK_CALLBACK
  // Set the link callback to notify USB host about link state changes
  netif_set_link_callback(netif, usbnet_netif_link_callback);
  netif_set_link_up(netif);
#else
  tud_network_link_state(BOARD_TUD_RHPORT, true);
#endif
  netif_set_status_callback(netif, ip_status_callback);
  netif_set_up(netif);
  s_usb_netif_initialized = true;
  LOGI(TAG, "USB netif ready: IP=192.168.7.1 mask=255.255.255.0");
}

static bool init_usb_netif(void) {
  s_usb_netif_initialized = false;
  err_t callback_result = tcpip_callback(init_usb_netif_cb, NULL);
  if (callback_result != ERR_OK) {
      LOGE(TAG, "Failed to schedule USB netif init callback: %d", callback_result);
      return false;
  }
  TickType_t deadline = xTaskGetTickCount() +
                        pdMS_TO_TICKS(USB_NETIF_LWIP_INIT_TIMEOUT_MS);
  while (!s_usb_netif_initialized &&
         (int32_t)(deadline - xTaskGetTickCount()) > 0) {
      vTaskDelay(pdMS_TO_TICKS(1U));
  }
  if (!s_usb_netif_initialized) {
      LOGE(TAG, "Timed out waiting for lwIP USB netif init callback");
      return false;
  }
  return true;
}

/* handle any DNS requests from dns-server */
static bool dns_query_proc(const char *name, ip4_addr_t *addr) {
  if (0 == strcmp(name, "tiny.usb")) {
    *addr = ipaddr;
    return true;
  }
  return false;
}

bool tud_network_recv_cb(const uint8_t *src, uint16_t size) {
  struct netif *netif = &netif_data;

  if (size) {
    struct pbuf *p = pbuf_alloc(PBUF_RAW, size, PBUF_POOL);

    if (p == NULL) {
      LOGD(TAG,"ERROR: Failed to allocate pbuf of size %d", size);
      return false;
    }

    /* Copy buf to pbuf */
    pbuf_take(p, src, size);

    // Surrender ownership of our pbuf unless there was an error
    // Only call pbuf_free if not Ok else it will panic with "pbuf_free: p->ref > 0"
    // or steal it from whatever took ownership of it with undefined consequences.
    // See: https://savannah.nongnu.org/patch/index.php?10121
    if (netif->input(p, netif) != ERR_OK) {
      LOGD(TAG,"ERROR: netif input failed");
      pbuf_free(p);
    }
    // Signal tinyusb that the current frame has been processed.
    tud_network_recv_renew();
  }

  return true;
}

uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg) {
  struct pbuf *p = (struct pbuf *) ref;

  (void) arg; /* unused for this example */
  return pbuf_copy_partial(p, dst, p->tot_len, 0);
}

static void led_blinking_task(void) {
  static uint32_t start_ms = 0;
  static bool led_state = false;

  // Blink every interval ms
  if (tusb_time_millis_api() - start_ms < blink_interval_ms) {
    return; // not enough time
  }
  start_ms += blink_interval_ms;
  led_state ? HAL_GPIO_WritePin(GPIOC,GPIO_PIN_13,GPIO_PIN_SET) : HAL_GPIO_WritePin(GPIOC,GPIO_PIN_13,GPIO_PIN_RESET);
  led_state = 1 - led_state; // toggle
}


void usb_netif_task(void *arg) {
  (void)arg;

  LOGI(TAG, "USB task entered");


  /* Wait for lwIP to be ready before initializing network interface */
  LOGI(TAG, "Waiting for lwIP ready");
  network_wait_lwip_ready();
  LOGI(TAG, "lwIP ready");

  /* initialize TinyUSB */

  // init device stack on configured roothub port
  tusb_rhport_init_t dev_init = {
    .role = TUSB_ROLE_DEVICE,
    .speed = TUSB_SPEED_AUTO
  };
  LOGI(TAG, "TinyUSB init enter");
  bool tusb_ok = tusb_init(BOARD_TUD_RHPORT, &dev_init);
  if (!tusb_ok) {
    LOGE(TAG, "TinyUSB initialization failed");
    vTaskDelete(NULL);
    return;
  }
  g_usb_tusb_initialized = 1U;
  LOGI(TAG, "TinyUSB init return: OK");
  LOGI(TAG, "USB controller: ISTR=0x%08lX CNTR=0x%08lX BCDR=0x%08lX DADDR=0x%08lX IRQ=%lu",
       (unsigned long)USB_DRD_FS->ISTR,
       (unsigned long)USB_DRD_FS->CNTR,
       (unsigned long)USB_DRD_FS->BCDR,
       (unsigned long)USB_DRD_FS->DADDR,
       (unsigned long)g_usb_irq_count);
  /* initialize usb network interface */
  LOGI(TAG, "USB netif init enter");
  if (!init_usb_netif()) {
    LOGE(TAG, "USB netif initialization failed; USB config disabled");
    vTaskDelete(NULL);
    return;
  }
  LOGI(TAG, "USB netif init return");
  
  LOGI(TAG, "Waiting for USB device ready");
  TickType_t mount_deadline = xTaskGetTickCount() +
                              pdMS_TO_TICKS(USB_NETIF_MOUNT_TIMEOUT_MS);
  while (!tud_ready() &&
         (int32_t)(mount_deadline - xTaskGetTickCount()) > 0) {
    tud_task_ext(5U, false);
    vTaskDelay(pdMS_TO_TICKS(1U));
  }
  if (!tud_ready()) {
    LOGW(TAG, "USB device not mounted after %u ms; continuing without USB config",
         (unsigned)USB_NETIF_MOUNT_TIMEOUT_MS);
  } else {
    LOGI(TAG, "USB device ready");
  }
  
  /* wait for netif to be up */
  LOGI(TAG, "Waiting for USB netif UP");
  if (!netif_is_up(&netif_data)) {
    LOGW(TAG, "USB netif is not UP; continuing without USB config");
  } else {
    LOGI(TAG, "USB netif UP");
  }
  s_usb_link_request = tud_mounted();
  (void)tcpip_callback(usb_netif_services_work, NULL);



#if CFG_TUD_NCM
  LOGD(TAG,"USB NCM network interface initialized");
#elif CFG_TUD_ECM_RNDIS
  LOGD(TAG,"USB RNDIS/ECM network interface initialized");
#endif

  TickType_t last_heartbeat = xTaskGetTickCount();
  while (1) {
    tud_task_ext(5U, false);
    // sys_check_timeouts(); // service lwip
    led_blinking_task();
    if ((xTaskGetTickCount() - last_heartbeat) >= pdMS_TO_TICKS(5000U)) {
      LOGI(TAG, "USB task heartbeat: mounted=%d ready=%d available=%d",
           tud_mounted() ? 1 : 0, tud_ready() ? 1 : 0,
           usb_netif_is_available() ? 1 : 0);
      LOGI(TAG, "USB diagnostics: irq=%lu last_istr=0x%08lX istr=0x%08lX cntr=0x%08lX bcdr=0x%08lX daddr=0x%08lX",
           (unsigned long)g_usb_irq_count,
           (unsigned long)g_usb_last_istr,
           (unsigned long)USB_DRD_FS->ISTR,
           (unsigned long)USB_DRD_FS->CNTR,
           (unsigned long)USB_DRD_FS->BCDR,
           (unsigned long)USB_DRD_FS->DADDR);
      last_heartbeat = xTaskGetTickCount();
    }
    vTaskDelay(1);
  }
}

TaskHandle_t usb_netif_handle = NULL;

void usb_netif_init(){
#if !GW_USB_NETIF_ENABLE
  LOGW(TAG, "USB netif disabled for isolation test");
  usb_netif_handle = NULL;
  return;
#else
  LOGI(TAG, "Creating USB netif task at priority %u", (unsigned)USB_NETIF_TASK_PRIORITY);
  BaseType_t result = xTaskCreate(usb_netif_task,"usb_netif_process",4096,NULL,
                                  USB_NETIF_TASK_PRIORITY,&usb_netif_handle);
  if (result != pdPASS) {
    LOGE(TAG, "USB netif task creation failed");
  }
#endif
}

bool usb_netif_is_available(void) {
  return s_usb_available && s_usb_services_started;
}

// Invoked when device is mounted
void tud_mount_cb(void) {
  blink_interval_ms = BLINK_MOUNTED;
  s_usb_link_request = true;
  (void)tcpip_callback(usb_netif_link_work, NULL);
  LOGI(TAG, "USB host mounted; Web Config available at http://192.168.7.1/");
}

// Invoked when device is unmounted
void tud_umount_cb(void) {
  blink_interval_ms = BLINK_NOT_MOUNTED;
  s_usb_link_request = false;
  (void)tcpip_callback(usb_netif_link_work, NULL);
  LOGI(TAG, "USB host unmounted; USB Web Config unavailable");
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en) {
  (void) remote_wakeup_en;
  blink_interval_ms = BLINK_SUSPENDED;
}

// Invoked when usb bus is resumed
void tud_resume_cb(void) {
  blink_interval_ms = tud_mounted() ? BLINK_MOUNTED : BLINK_NOT_MOUNTED;
}

#else  /* GW_USB_CDC_ENABLE */

#include <stdbool.h>

void usb_netif_init(void) {
  /* Nothing to start: the USB device is created by gw_usb_cdc_start(). */
}

bool usb_netif_is_available(void) {
  return false;  /* no USB network interface in CDC-only mode */
}

#endif /* GW_USB_CDC_ENABLE */