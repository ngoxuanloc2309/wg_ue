/*
 * gw_usb_cdc.c - CDC-ACM only USB device of the gateway (see gw_usb_cdc.h).
 *
 * Modelled on SimplePLC's proven splc_usb_cdc.c (Zigbee-IO board).
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
#include "modbus_link.h"

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
/* Same size as TinyUSB's CDC RX FIFO packet (Full-Speed bulk maximum). */
#define GW_USB_CDC_RX_CHUNK             64U

#ifndef GW_USB_CDC_HAL_PCD_INIT
/* 1 = call MX_USB_PCD_Init() (HAL_PCD_Init) before tusb_init(), exactly like the
 * proven Zigbee-IO board; 0 = TinyUSB alone (bisect switch). */
#define GW_USB_CDC_HAL_PCD_INIT         1
#endif
#ifndef GW_USB_CDC_START_DELAY_MS
/* Delay before USB starts. 0 = bring USB up as soon as the scheduler runs, like
 * the Zigbee-IO board. Set it (e.g. -DGW_USB_CDC_START_DELAY_MS=8000) only to
 * test whether the A7680C modem, which shares the D+/D- net with the Type-C
 * connector (Documents/Hardware/02_PINOUT_AND_INTERFACES.md), disturbs the
 * attach while it powers up. */
#define GW_USB_CDC_START_DELAY_MS       0U
#endif

#ifndef GW_USB_CDC_PIN_MODE
/* What to do with PA11 (D-) / PA12 (D+) before USB starts:
 *   0 = leave them as Core/Src/gpio.c set them (AF10 push-pull). This is the
 *       configuration with which the PC DID see the board (bus reset reached the
 *       MCU) in the old USB-netif build, so it is the default.
 *   1 = analog (reset state): what the Zigbee-IO board runs. With it the gateway
 *       log showed irq=0, i.e. the PC saw nothing.
 *   2 = digital input (TinyUSB's own stm32h5 BSP). */
#define GW_USB_CDC_PIN_MODE             1
#endif

static const char *TAG = "usb-cdc";

static volatile bool s_started;   /* task created */
static volatile bool s_ready;     /* tusb_init() done */

/* ---------------------------------------------------------------------- */
/* Bring-up helpers                                                         */
/* ---------------------------------------------------------------------- */

static void gw_usb_cdc_pins_to_phy(void)
{
#if GW_USB_CDC_PIN_MODE != 0
    GPIO_InitTypeDef pins = {0};
    pins.Pin  = GPIO_PIN_11 | GPIO_PIN_12;
    pins.Mode = (GW_USB_CDC_PIN_MODE == 1) ? GPIO_MODE_ANALOG : GPIO_MODE_INPUT;
    pins.Pull = GPIO_NOPULL;
    pins.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &pins);
#endif
    LOGI(TAG, "PA11/PA12 MODER=%lu%lu AFRH=0x%08lX (pin mode option %d: 0 = as gpio.c)",
         (unsigned long)((GPIOA->MODER >> 24) & 3U), (unsigned long)((GPIOA->MODER >> 22) & 3U),
         (unsigned long)GPIOA->AFR[1], (int)GW_USB_CDC_PIN_MODE);
}

/* One-shot dump of what the USB block depends on: HSI48 running, USB kernel
 * clock source, bus clock, and the VDDUSB supply flags. */
static void gw_usb_cdc_log_clock_power(void)
{
    /* USB33RDY only means something while the VDDUSB detector is on. Enabling it
     * is harmless (it is a measurement, it does not change the supply). */
    HAL_PWREx_EnableUSBVoltageDetector();
    vTaskDelay(pdMS_TO_TICKS(2));
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

/* Until the PC enumerates the device these show how far the handshake got:
 * irq=0 means the PC never reset the bus (no attach seen: pull-up, cable or the
 * shared modem net); irq>0 but never mounted means reset seen, enumeration
 * failed (clock accuracy, task latency). BCDR bit 15 = DPPU (D+ pull-up). */
static void gw_usb_cdc_log_diag(void)
{
    LOGI(TAG, "not enumerated: irq=%lu last_istr=0x%08lX istr=0x%08lX "
              "cntr=0x%08lX bcdr=0x%08lX daddr=0x%08lX tud_inited=%d",
         (unsigned long)g_usb_irq_count,
         (unsigned long)g_usb_last_istr,
         (unsigned long)USB_DRD_FS->ISTR,
         (unsigned long)USB_DRD_FS->CNTR,
         (unsigned long)USB_DRD_FS->BCDR,
         (unsigned long)USB_DRD_FS->DADDR,
         tud_inited() ? 1 : 0);
}

/* ---------------------------------------------------------------------- */
/* USB task                                                                 */
/* ---------------------------------------------------------------------- */

static void gw_usb_cdc_task(void *arg)
{
    (void)arg;

#if GW_USB_CDC_START_DELAY_MS > 0U
    vTaskDelay(pdMS_TO_TICKS(GW_USB_CDC_START_DELAY_MS));
#endif

    gw_usb_cdc_pins_to_phy();

#if GW_USB_CDC_HAL_PCD_INIT
    /* Same as the Zigbee-IO board (Core/Src/main.c: MX_USB_PCD_Init() before
     * tusb_init()): the HAL selects HSI48 as USB kernel clock, enables VDDUSB and
     * the USB clock, and resets the USB_DRD_FS core. HAL_PCD_MspInit() of this
     * project does not touch the NVIC, so no interrupt can reach
     * tud_int_handler() before tusb_init() below. */
    MX_USB_PCD_Init();
    LOGI(TAG, "HAL_PCD_Init done");
#endif

    /* Clock (HSI48), VDDUSB and NVIC at a FreeRTOS-safe priority; clears the
     * interrupt counters and g_usb_tusb_initialized. */
    MX_USB_TinyUSB_Init();

    const tusb_rhport_init_t dev_init =
    {
        .role  = TUSB_ROLE_DEVICE,
        /* USB_DRD_FS (stm32_fsdev) is Full-Speed only on STM32H5. */
        .speed = TUSB_SPEED_FULL
    };
    if (!tusb_init(BOARD_TUD_RHPORT, &dev_init))
    {
        LOGE(TAG, "tusb_init failed; USB CDC disabled");
        vTaskDelete(NULL);
        return;
    }
    /* The USB interrupt enters tud_int_handler() only after this. */
    g_usb_tusb_initialized = 1U;
    s_ready = true;
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

        /* Diagnostics only. Nothing here touches the bus: no disconnect /
         * connect cycling (SimplePLC does none either), so a slow host is not
         * interrupted mid-enumeration. */
        if (!is_mounted)
        {
            TickType_t now = xTaskGetTickCount();
            if ((TickType_t)(now - last_diag) >= pdMS_TO_TICKS(GW_USB_CDC_DIAG_PERIOD_MS))
            {
                last_diag = now;
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

bool gw_usb_cdc_ready(void)
{
    return s_ready;
}

bool gw_usb_cdc_connected(void)
{
    return s_ready && tud_cdc_n_connected(GW_USB_CDC_CDC_ITF);
}

/* ---------------------------------------------------------------------- */
/* modbus_link pipe (same logic as splc_usb_cdc.c)                          */
/* ---------------------------------------------------------------------- */

void gw_usb_cdc_process(void *ctx)
{
    (void)ctx;

    if (!gw_usb_cdc_connected())
    {
        return;
    }

    /* Drain the ENTIRE TinyUSB FIFO, not one chunk per call: a frame spanning
     * several 64-byte packets must reach the link as soon as it is in the FIFO,
     * or nanoMODBUS misreads the tail as a new request. This only moves bytes
     * that ARE already in the FIFO; it never waits. */
    uint8_t buf[GW_USB_CDC_RX_CHUNK];
    while (tud_cdc_n_available(GW_USB_CDC_CDC_ITF) != 0U)
    {
        uint32_t count = tud_cdc_n_read(GW_USB_CDC_CDC_ITF, buf, sizeof(buf));
        if (count == 0U)
        {
            break;   /* available() said > 0 but read() got nothing */
        }
        if (splc_link_input(buf, (uint16_t)count) < count)
        {
            /* The link's RX ring is full (it counts the dropped bytes). Stop
             * draining: the rest stays in TinyUSB's FIFO and pushes back on the
             * host. */
            break;
        }
    }
}

int32_t gw_usb_cdc_output(const uint8_t *buf, uint16_t len, void *ctx)
{
    (void)ctx;

    if (len == 0U)
    {
        return 0;
    }
    if (!gw_usb_cdc_connected())
    {
        return -1;   /* hard error: no host, or the App closed the port (DTR low) */
    }

    uint32_t accepted = tud_cdc_n_write(GW_USB_CDC_CDC_ITF, buf, len);
    (void)tud_cdc_n_write_flush(GW_USB_CDC_CDC_ITF);   /* start the transfer now */
    return (int32_t)accepted;                           /* 0 = FIFO full, core retries */
}