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
/* Same size as TinyUSB's CDC RX FIFO packet (Full-Speed bulk maximum). */
#define GW_USB_CDC_RX_CHUNK             64U

static const char *TAG = "usb-cdc";

static volatile bool s_started;   /* task created */
static volatile bool s_ready;     /* tusb_init() done */

/* PA11 (D-) / PA12 (D+): analog, the reset state, so the USB PHY owns the lines
 * (the Zigbee-IO board and ST's H5 USB examples run like this). Core/Src/gpio.c
 * sets them to AF10 push-pull; hand them back to the PHY before USB starts. */
static void gw_usb_cdc_pins_to_phy(void)
{
    GPIO_InitTypeDef pins = {0};
    pins.Pin  = GPIO_PIN_11 | GPIO_PIN_12;
    pins.Mode = GPIO_MODE_ANALOG;
    pins.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &pins);
}

/* ---------------------------------------------------------------------- */
/* USB task                                                                 */
/* ---------------------------------------------------------------------- */

static void gw_usb_cdc_task(void *arg)
{
    (void)arg;

    gw_usb_cdc_pins_to_phy();

    /* Same as the Zigbee-IO board (Core/Src/main.c: MX_USB_PCD_Init() before
     * tusb_init()): the HAL selects HSI48 as USB kernel clock, enables VDDUSB and
     * the USB clock, and resets the USB_DRD_FS core. HAL_PCD_MspInit() of this
     * project does not touch the NVIC, so no interrupt can reach
     * tud_int_handler() before tusb_init() below. */
    MX_USB_PCD_Init();

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

    bool was_mounted = false;
    bool was_open = false;

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