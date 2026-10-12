/*
 * gw_usb_cdc.c - CDC-ACM only USB device of the gateway (see gw_usb_cdc.h).
 */
#include "gw_usb_cdc.h"

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#include "tusb.h"
#include "main.h"
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

/* ---- temporary boot-time register sampler (diagnostics only) ---------- */
typedef struct { uint32_t tick, chep0, istr, cntr, daddr, irq, last; } usb_diag_t;
#define USB_DIAG_MAX 24U
static usb_diag_t s_diag[USB_DIAG_MAX];
static uint32_t   s_diag_n;

static void diag_sample(bool force)
{
    usb_diag_t cur;
    cur.tick  = (uint32_t)xTaskGetTickCount();
    cur.chep0 = USB_DRD_FS->CHEP0R;
    cur.istr  = USB_DRD_FS->ISTR;
    cur.cntr  = USB_DRD_FS->CNTR;
    cur.daddr = USB_DRD_FS->DADDR;
    cur.irq   = g_usb_irq_count;
    cur.last  = g_usb_last_istr;
    if (s_diag_n >= USB_DIAG_MAX) { return; }
    if (!force && s_diag_n > 0U)
    {
        const usb_diag_t *p = &s_diag[s_diag_n - 1U];
        if (p->chep0 == cur.chep0 && p->istr == cur.istr && p->cntr == cur.cntr &&
            p->daddr == cur.daddr && p->irq == cur.irq)
        {
            return;
        }
    }
    s_diag[s_diag_n++] = cur;
}

static void diag_print(TickType_t t0)
{
    for (uint32_t i = 0; i < s_diag_n; i++)
    {
        const usb_diag_t *d = &s_diag[i];
        LOGI(TAG, "diag[%lu] t=%ldms chep0r=0x%08lX istr=0x%08lX cntr=0x%08lX daddr=0x%02lX irq=%lu last=0x%08lX",
             (unsigned long)i, (long)((int32_t)(d->tick - (uint32_t)t0)),
             (unsigned long)d->chep0, (unsigned long)d->istr, (unsigned long)d->cntr,
             (unsigned long)d->daddr, (unsigned long)d->irq, (unsigned long)d->last);
    }
}

static void gw_usb_cdc_log_diag(void)
{
    LOGI(TAG, "not enumerated: irq=%lu last_istr=0x%08lX istr=0x%08lX "
              "cntr=0x%08lX bcdr=0x%08lX daddr=0x%08lX chep0r=0x%08lX inited=%d",
         (unsigned long)g_usb_irq_count,
         (unsigned long)g_usb_last_istr,
         (unsigned long)USB_DRD_FS->ISTR,
         (unsigned long)USB_DRD_FS->CNTR,
         (unsigned long)USB_DRD_FS->BCDR,
         (unsigned long)USB_DRD_FS->DADDR,
         (unsigned long)USB_DRD_FS->CHEP0R,
         tud_inited() ? 1 : 0);
}

static void gw_usb_cdc_task(void *arg)
{
    (void)arg;

    /* EXPERIMENT: MX_GPIO_Init() (CubeMX, not edited) leaves PA11/PA12 as
     * AF_PP / AF10 / high speed, i.e. a push-pull output driver on D-/D+.
     * The working SimplePLC board never configures these pins, and the
     * TinyUSB STM32H5 reference BSP sets them to plain input. Put them back in
     * analog mode (reset state) so only the USB transceiver drives the lines. */
    {
        GPIO_InitTypeDef usb_pins = {0};
        __HAL_RCC_GPIOA_CLK_ENABLE();
        usb_pins.Pin  = GPIO_PIN_11 | GPIO_PIN_12;
        usb_pins.Mode = GPIO_MODE_ANALOG;
        usb_pins.Pull = GPIO_NOPULL;
        HAL_GPIO_Init(GPIOA, &usb_pins);
    }

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
    /* DIAG (temporary): sample the USB registers for the first 600 ms and log
     * every change, to see WHEN EP0 (CHEP0R) is cleared and what ISTR/IRQ did. */
    diag_sample(true);
    /* The USB interrupt enters tud_int_handler() only after this. */
    g_usb_tusb_initialized = 1U;
    LOGI(TAG, "TinyUSB device started (CDC-ACM only)");
    const TickType_t diag_t0 = xTaskGetTickCount();
    bool diag_printed = false;

    bool was_mounted = false;
    bool was_open = false;
    TickType_t last_diag = xTaskGetTickCount();

    for (;;)
    {
        if (!diag_printed)
        {
            diag_sample(false);
            if ((TickType_t)(xTaskGetTickCount() - diag_t0) >= pdMS_TO_TICKS(600U))
            {
                diag_printed = true;
                diag_print(diag_t0);
            }
            tud_task_ext(1U, false);
        }
        else
        {
            tud_task_ext(GW_USB_CDC_EVENT_WAIT_MS, false);
        }

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