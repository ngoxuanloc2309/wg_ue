/*
 * gw_usb_cdc.c - CDC-ACM only USB device of the gateway (see gw_usb_cdc.h).
 */
#include "gw_usb_cdc.h"

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#include "tusb.h"
#include "usb.h"
#include "logger.h"

#define GW_USB_CDC_CDC_ITF              0U
#define GW_USB_CDC_TASK_NAME            "USB_CDC"
/* Words (4 bytes), taken from the FreeRTOS heap. */
#define GW_USB_CDC_TASK_STACK_WORDS     1024U
/* Same band as the former usb_netif_task; the SimplePLC task (6) stays above
 * it so the 10 ms scan keeps its period. */
#define GW_USB_CDC_TASK_PRIO            5U
/* tud_task_ext() blocks on TinyUSB's event queue; an interrupt wakes it at
 * once, so this only bounds how late the state logging below reacts. */
#define GW_USB_CDC_EVENT_WAIT_MS        5U
#define GW_USB_CDC_DIAG_PERIOD_MS       5000U

static const char *TAG = "usb-cdc";

static bool s_started;

static void gw_usb_cdc_log_diag(void)
{
    LOGI(TAG, "not enumerated: irq=%lu last_istr=0x%08lX istr=0x%08lX "
              "cntr=0x%08lX bcdr=0x%08lX daddr=0x%08lX",
         (unsigned long)g_usb_irq_count,
         (unsigned long)g_usb_last_istr,
         (unsigned long)USB_DRD_FS->ISTR,
         (unsigned long)USB_DRD_FS->CNTR,
         (unsigned long)USB_DRD_FS->BCDR,
         (unsigned long)USB_DRD_FS->DADDR);
}

static void gw_usb_cdc_task(void *arg)
{
    (void)arg;

    /* Clock (HSI48), VDDUSB and NVIC; clears g_usb_tusb_initialized. */
    MX_USB_TinyUSB_Init();

    const tusb_rhport_init_t dev_init =
    {
        .role  = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_AUTO
    };
    if (!tusb_init(BOARD_TUD_RHPORT, &dev_init))
    {
        LOGE(TAG, "TinyUSB initialization failed; USB CDC disabled");
        vTaskDelete(NULL);
        return;
    }
    /* The USB interrupt enters tud_int_handler() only after this. */
    g_usb_tusb_initialized = 1U;
    LOGI(TAG, "TinyUSB device started (CDC-ACM only)");

    bool was_mounted = false;
    bool was_open = false;
    TickType_t last_diag = xTaskGetTickCount();

    for (;;)
    {
        tud_task_ext(GW_USB_CDC_EVENT_WAIT_MS, false);

        bool is_mounted = tud_mounted();
        if (is_mounted != was_mounted)
        {
            was_mounted = is_mounted;
            LOGI(TAG, "USB host %s", is_mounted ? "enumerated the device" : "detached");
        }

        bool is_open = is_mounted && tud_cdc_n_connected(GW_USB_CDC_CDC_ITF);
        if (is_open != was_open)
        {
            was_open = is_open;
            LOGI(TAG, "COM port %s by the host", is_open ? "opened" : "closed");
        }

        /* Until the PC enumerates the device the interrupt counters show how
         * far the handshake got (reset only, address set, ...). */
        TickType_t now = xTaskGetTickCount();
        bool diag_due = (TickType_t)(now - last_diag) >= pdMS_TO_TICKS(GW_USB_CDC_DIAG_PERIOD_MS);
        if (diag_due)
        {
            last_diag = now;
            if (!is_mounted)
            {
                gw_usb_cdc_log_diag();
            }
        }
    }
}

bool gw_usb_cdc_start(void)
{
    if (s_started)
    {
        LOGE(TAG, "gw_usb_cdc_start() called twice");
        return false;
    }

    BaseType_t created = xTaskCreate(gw_usb_cdc_task, GW_USB_CDC_TASK_NAME,
                                     (configSTACK_DEPTH_TYPE)GW_USB_CDC_TASK_STACK_WORDS,
                                     NULL, GW_USB_CDC_TASK_PRIO, NULL);
    if (created != pdPASS)
    {
        LOGE(TAG, "USB CDC task creation failed");
        return false;
    }

    s_started = true;
    return true;
}