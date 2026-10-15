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
/* The SimplePLC task (prio 6) stays above it so the 10 ms scan keeps its period. */
#define GW_USB_CDC_TASK_PRIO            5U
/* tud_task_ext() blocks on TinyUSB's event queue; an interrupt wakes it at
 * once, so this only bounds how late the state logging below reacts. */
#define GW_USB_CDC_EVENT_WAIT_MS        5U
#define GW_USB_CDC_DIAG_PERIOD_MS       5000U
#ifndef GW_USB_CDC_HAL_PCD_INIT
/* 1 = call MX_USB_PCD_Init() (HAL_PCD_Init) before tusb_init(), like the proven
 * Zigbee-IO board; 0 = TinyUSB alone. */
#define GW_USB_CDC_HAL_PCD_INIT         1
#endif
#ifndef GW_USB_CDC_START_DELAY_MS
/* Delay before USB starts, see gw_usb_cdc_task(). */
#define GW_USB_CDC_START_DELAY_MS       8000U
#endif

static const char *TAG = "usb-cdc";

static bool s_started;

/* One-shot dump of what the USB block depends on: HSI48 running, USB kernel
 * clock source, bus clock, and the VDDUSB supply flags. */
static void gw_usb_cdc_log_clock_power(void)
{
    LOGI(TAG, "clk/pwr: HSI48ON=%d HSI48RDY=%d USBSEL=%lu (3=HSI48) USBEN=%d "
              "USB33SV=%d USB33DEN=%d USB33RDY=%d",
         (RCC->CR & RCC_CR_HSI48ON) ? 1 : 0,
         (RCC->CR & RCC_CR_HSI48RDY) ? 1 : 0,
         (unsigned long)((RCC->CCIPR4 & RCC_CCIPR4_USBSEL) >> RCC_CCIPR4_USBSEL_Pos),
         (RCC->APB2ENR & RCC_APB2ENR_USBEN) ? 1 : 0,
         (PWR->USBSCR & PWR_USBSCR_USB33SV) ? 1 : 0,
         (PWR->USBSCR & PWR_USBSCR_USB33DEN) ? 1 : 0,
         (PWR->VMSR & PWR_VMSR_USB33RDY) ? 1 : 0);
}

static void gw_usb_cdc_log_diag(void)
{
    LOGI(TAG, "not enumerated: irq=%lu last_istr=0x%08lX istr=0x%08lX "
              "cntr=0x%08lX bcdr=0x%08lX daddr=0x%08lX inited=%d "
              "PA11(D-)=%lu PA12(D+)=%lu",
         (unsigned long)g_usb_irq_count,
         (unsigned long)g_usb_last_istr,
         (unsigned long)USB_DRD_FS->ISTR,
         (unsigned long)USB_DRD_FS->CNTR,
         (unsigned long)USB_DRD_FS->BCDR,
         (unsigned long)USB_DRD_FS->DADDR,
         tud_inited() ? 1 : 0,
         (unsigned long)((GPIOA->IDR >> 11) & 1U),
         (unsigned long)((GPIOA->IDR >> 12) & 1U));
}

static void gw_usb_cdc_task(void *arg)
{
    (void)arg;

    /* PA11 (D-) / PA12 (D+): digital input, as in TinyUSB's own stm32h5 BSP
     * (hw/bsp/stm32h5/family.c). The USB PHY still owns the lines; the input
     * stage only lets us read their level for the diagnostic log below. */
    {
        GPIO_InitTypeDef usb_pins = {0};
        usb_pins.Pin   = GPIO_PIN_11 | GPIO_PIN_12;
        usb_pins.Mode  = GPIO_MODE_INPUT;
        usb_pins.Pull  = GPIO_NOPULL;
        usb_pins.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(GPIOA, &usb_pins);
    }

    /* The USB netif build only starts USB after WiFi has an IP (several seconds
     * after boot), when the shared D+/D- net with the A7680C modem has settled.
     * Starting at once overlaps with the modem power-on, so wait the same way. */
    vTaskDelay(pdMS_TO_TICKS(GW_USB_CDC_START_DELAY_MS));

#if GW_USB_CDC_HAL_PCD_INIT
    /* Same bring-up as SimplePLC's proven Zigbee-IO board (Core/Src/main.c calls
     * MX_USB_PCD_Init() = HAL_PCD_Init() before splc_usb_cdc_init() -> tusb_init()):
     * the HAL powers up and resets the USB_DRD_FS core, then TinyUSB takes over.
     * HAL_PCD_MspInit() here does not enable the NVIC, so no interrupt can reach
     * tud_int_handler() before tusb_init() below. */
    MX_USB_PCD_Init();
    LOGI(TAG, "HAL_PCD_Init done");
#endif

    /* Clock (HSI48), VDDUSB and NVIC; clears g_usb_tusb_initialized. */
    MX_USB_TinyUSB_Init();

    const tusb_rhport_init_t dev_init =
    {
        .role  = TUSB_ROLE_DEVICE,
        /* USB_DRD_FS is Full-Speed only on STM32H5; SimplePLC's proven module uses
         * TUSB_SPEED_FULL too (TUSB_SPEED_AUTO left this ambiguous). */
        .speed = TUSB_SPEED_FULL
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
    gw_usb_cdc_log_clock_power();

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
        if ((TickType_t)(now - last_diag) >= pdMS_TO_TICKS(GW_USB_CDC_DIAG_PERIOD_MS))
        {
            last_diag = now;
            if (!is_mounted)
            {
                gw_usb_cdc_log_diag();
                /* Drop and re-raise the D+ pull-up so the host sees a fresh attach. */
                tud_disconnect();
                vTaskDelay(pdMS_TO_TICKS(100));
                tud_connect();
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