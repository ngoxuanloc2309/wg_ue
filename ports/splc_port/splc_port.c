/*
 * splc_port.c - GatewayV2 host port of SimplePLC (see splc_port.h).
 */
#include "splc_port.h"

#include <stdint.h>
#include <stdbool.h>

#include "FreeRTOS.h"
#include "task.h"
#if !SPLC_HOST_APP_LINK_RS485 && !GW_USB_CDC_ENABLE
#include "tusb.h"   /* NETIF build: usb_netif_task owns USB, the pipe below uses tud_*() */
#endif
#include "rs485_driver.h"

#include "plc_engine.h"
#include "splc_mqtt_port.h"
#include "splc_modbus_tcp_port.h"
#include "modbus_link.h"
#include "modbus_serial.h"
#include "sx_rtc.h"
#include "sx_adc.h"
#include "board_ethernet_wifi_gateway.h"
#include "splc_log.h"
#include "gw_rs485_diag.h"

#if GW_USB_CDC_ENABLE
#include "gw_usb_cdc.h"
#endif

static const char *TAG = "SPLC_PORT";

#if !SPLC_HOST_APP_LINK_RS485 && !GW_USB_CDC_ENABLE
/* The CDC-ACM is the only CDC class instance of the host's descriptor
 * (ports/usb_port/Target/usb_descriptors.c, or usb_descriptors_cdc.c with
 * GW_USB_CDC_ENABLE); the network function is RNDIS / ECM / NCM, not a CDC
 * class instance. */
#define SPLC_CDC_ITF            0

/* Set by usb_netif_task (or gw_usb_cdc_task) after tusb_init() succeeded
 * (Core/Src/usb.c).
 * Until then no tud_*() call is allowed. */
extern volatile uint8_t g_usb_tusb_initialized;
#endif /* !SPLC_HOST_APP_LINK_RS485 && !GW_USB_CDC_ENABLE */

#define SPLC_PORT_CHECK_MS      1000U

/* ------------------------------------------------------------------------ */
/* RS485: modbus_serial_port_t on top of the host driver                     */
/* ------------------------------------------------------------------------ */

/* Non-blocking read. NOTE: rs485_recv_polling() counts every empty read in
 * counters.timeouts of this port (host driver behaviour, left untouched), so
 * that counter is meaningless for the SimplePLC port. */
static int rs485_read_cb(void *ctx, uint8_t *buf, uint16_t max_len)
{
    (void)ctx;
    int32_t n = rs485_recv_polling((uint8_t)SPLC_HOST_RS485_PORT, buf, max_len, 0U);
    return (n > 0) ? (int)n : 0;
}

/* rs485_send() drives DE itself and returns after the last bit left the UART
 * (it waits for TC), which is exactly the contract of modbus_serial_port_t.
 * Its own timeout (1 s) is fixed inside the driver, so timeout_ms is unused. */
static int rs485_write_cb(void *ctx, const uint8_t *buf, uint16_t len, uint32_t timeout_ms)
{
    (void)ctx;
    (void)timeout_ms;
    int32_t n = rs485_send((uint8_t)SPLC_HOST_RS485_PORT, buf, len);
    return (n == (int32_t)len) ? 0 : -1;
}

/* One RS485 port this file keeps ours: the I/O boards bus, and the App link
 * port when SPLC_HOST_APP_RS485_PORT is used. */
typedef struct {
    uint8_t     port;
    uint32_t    baud;
    uint8_t     parity;            /* 0 none, 1 odd, 2 even */
    const char *name;              /* for the log */
    int8_t      last_problem;      /* -1 none yet, 0 ok, 1 owner, 2 init */
} splc_port_watch_t;

/*
 * Keep an RS485 port ours and configured. Runs in the engine task every
 * SPLC_PORT_CHECK_MS. The host can change the port under us (web UI, config
 * apply) or may not have initialised it yet, so this is a loop, not a one-off:
 *   - take ownership as RS485_OWNER_MODBUS_GATEWAY (fails while another
 *     service owns the port: then we only log, once per change),
 *   - (re)apply baud / parity when the port is not RUNNING or differs.
 * While this is not satisfied rs485_send()/recv return -1 and the virtual I/O
 * nodes (or the App link) simply stay unreachable; they recover on their own
 * afterwards.
 */
static void splc_check_port(splc_port_watch_t *w)
{
    const uint8_t port = w->port;
    rs485_port_status_t st;
    int8_t problem = 0;

    if (rs485_port_get_status(port, &st) != 0) {
        problem = 2;
    } else {
        if (st.owner == RS485_OWNER_NONE) {
            if (rs485_port_acquire(port, RS485_OWNER_MODBUS_GATEWAY) != 0) {
                problem = 2;             /* driver not initialised yet */
            } else {
                st.owner = RS485_OWNER_MODBUS_GATEWAY;
            }
        }
        if (problem == 0 && st.owner != RS485_OWNER_MODBUS_GATEWAY) {
            problem = 1;
        }
        if (problem == 0) {
            if (st.state != RS485_STATE_RUNNING ||
                st.config.baudrate != w->baud ||
                st.config.parity   != w->parity ||
                st.config.data_bits != 8U || st.config.stop_bits != 1U) {
                rs485_runtime_config_t cfg = {
                    .baudrate  = w->baud,
                    .data_bits = 8U,
                    .parity    = w->parity,
                    .stop_bits = 1U,
                };
                if (rs485_port_reconfigure(port, &cfg) != 0) {
                    problem = 2;
                }
            }
        }
    }

    if (problem != w->last_problem) {
        if (problem == 1) {
            log_error(TAG, "RS485 port %d (%s) is owned by another host service; "
                           "it is NOT served", (int)port, w->name);
        } else if (problem == 2) {
            log_warn(TAG, "RS485 port %d (%s) not ready (driver not initialised "
                          "or reconfigure failed), retrying", (int)port, w->name);
        } else if (w->last_problem > 0) {
            log_info(TAG, "RS485 port %d (%s) ready", (int)port, w->name);
        }
        w->last_problem = problem;
    }
}

static splc_port_watch_t s_io_watch = {
    .port = (uint8_t)SPLC_HOST_RS485_PORT, .baud = SPLC_HOST_RS485_BAUD,
    .parity = (uint8_t)SPLC_HOST_RS485_PARITY, .name = "I/O boards", .last_problem = -1,
};

#if SPLC_HOST_APP_LINK_RS485
/* ------------------------------------------------------------------------ */
/* App link: modbus_link ops on top of a second host RS485 port (slave)      */
/* ------------------------------------------------------------------------ */

/* The whole link is the two functions modbus_link asks a port for. Both run in
 * the engine task (modbus_link contract), which is also the only caller of
 * splc_link_input(). The host driver already owns the UART interrupt, the RX
 * stream buffer (RS485_RX_BUF_SIZE) and the DE pin: this file adds no UART
 * code. MAX485 has DE and /RE on one pin, so the receiver is off while we
 * transmit and our own frame is never read back as a request. */

#define SPLC_APP_PORT           ((uint8_t)SPLC_HOST_APP_RS485_PORT)

/* Called once per scan and while read()/write() wait: move received bytes into
 * the core's ring. NOTE: rs485_recv_polling() counts every empty read in
 * counters.timeouts of this port (host driver behaviour, left untouched), so
 * that counter is meaningless for the App port. */
static void app_process(void *ctx)
{
    (void)ctx;
    uint8_t tmp[64];
    for (;;) {
        int32_t n = rs485_recv_polling(SPLC_APP_PORT, tmp, sizeof(tmp), 0U);
        if (n <= 0) {
            break;               /* nothing queued, or the port is not RUNNING yet */
        }
        if (splc_link_input(tmp, (uint16_t)n) < (uint16_t)n) {
            break;               /* ring full: the core counted the dropped bytes */
        }
    }
}

/* rs485_send() drives DE and returns after the last bit left the UART. It blocks
 * the engine task for the frame time (~3.6 ms for 41 B, ~22 ms for 250 B at
 * 115200): modbus_link's "output does not block" is relaxed here on purpose. */
static int32_t app_output(const uint8_t *buf, uint16_t len, void *ctx)
{
    (void)ctx;
    if (len == 0U) {
        return 0;
    }
    int32_t n = rs485_send(SPLC_APP_PORT, buf, len);
    return (n == (int32_t)len) ? (int32_t)len : -1;   /* -1: port not ready / TX timeout */
}

static splc_port_watch_t s_app_watch = {
    .port = (uint8_t)SPLC_HOST_APP_RS485_PORT, .baud = SPLC_HOST_APP_RS485_BAUD,
    .parity = (uint8_t)SPLC_HOST_APP_RS485_PARITY, .name = "App link", .last_problem = -1,
};
#define app_link_output   app_output
#define app_link_process  app_process

#elif GW_USB_CDC_ENABLE
/* ------------------------------------------------------------------------ */
/* App link: modbus_link ops on top of gw_usb_cdc.c (the CDC-only USB owner)  */
/* ------------------------------------------------------------------------ */

/* The whole pipe lives in gw_usb_cdc.c, like splc_usb_cdc.c does for SimplePLC's
 * own USB module: this file only registers gw_usb_cdc_output / gw_usb_cdc_process
 * with modbus_link (see splc_host_start()). */
#define app_link_output   gw_usb_cdc_output
#define app_link_process  gw_usb_cdc_process

#else /* USB CDC through usb_netif_task (GW_USB_NETIF_ENABLE) */
/* ------------------------------------------------------------------------ */
/* App link: modbus_link ops on top of TinyUSB CDC instance 0                */
/* ------------------------------------------------------------------------ */

/* Everything below runs in the engine task (modbus_link contract). TinyUSB's
 * CDC FIFOs are protected by its OS mutex (CFG_TUSB_OS = FreeRTOS), so the
 * engine task may read/write them while usb_netif_task runs tud_task_ext();
 * a reply can therefore leave up to one scan (10 ms) late. */

static bool cdc_usable(void)
{
    return g_usb_tusb_initialized != 0U && tud_cdc_n_connected(SPLC_CDC_ITF);
}

/* Called once per scan and while read()/write() wait: move received bytes into
 * the core's ring. splc_link_input() is only ever called from here. */
static void cdc_process(void *ctx)
{
    (void)ctx;
    if (!cdc_usable()) {
        return;
    }
    uint8_t tmp[64];
    while (tud_cdc_n_available(SPLC_CDC_ITF) > 0U) {
        uint32_t n = tud_cdc_n_read(SPLC_CDC_ITF, tmp, sizeof(tmp));
        if (n == 0U) {
            break;
        }
        if (splc_link_input(tmp, (uint16_t)n) < n) {
            break;   /* ring full: the core counted the dropped bytes */
        }
    }
}

static int32_t cdc_output(const uint8_t *buf, uint16_t len, void *ctx)
{
    (void)ctx;
    if (!cdc_usable()) {
        return -1;   /* hard error: no USB, or the App closed the port (DTR low) */
    }
    uint32_t n = tud_cdc_n_write(SPLC_CDC_ITF, buf, len);
    (void)tud_cdc_n_write_flush(SPLC_CDC_ITF);
    return (int32_t)n;   /* 0 = FIFO full, the core retries */
}
#define app_link_output   cdc_output
#define app_link_process  cdc_process
#endif /* SPLC_HOST_APP_LINK_RS485 */

/* ------------------------------------------------------------------------ */
/* Peripherals the host does not have (SPLC_RTC_BUILTIN / SPLC_ADC_BUILTIN)  */
/* ------------------------------------------------------------------------ */

/* GatewayV2 has no CubeMX RTC and no analog input, so splcopts.h sets both
 * options to 0 and SimplePLC leaves its STM32H5 drivers out. The linker then
 * demands these functions (config/splc_opt.h). They are honest "not available"
 * answers, not fake data:
 *   - RTC: never synced -> Time Window rules do not fire, and an App write of
 *     the clock is rejected. Who owns the clock (host NTP) is still an open
 *     decision (handoff, decision 23).
 *   - ADC: the gateway has no AI tag, so nothing calls these; a read returns 0
 *     as the contract says for "timeout/error". */
#if (SPLC_RTC_BUILTIN == 0)
bool sx_rtc_set_epoch(uint32_t epoch_utc_s)
{
    (void)epoch_utc_s;
    return false;
}

bool sx_rtc_get_epoch(uint32_t *epoch_utc_s)
{
    (void)epoch_utc_s;
    return false;
}

bool sx_rtc_is_synced(void)
{
    return false;
}
#endif

#if (SPLC_ADC_BUILTIN == 0)
void sx_adc_init(sx_adc_config_t *config, sx_adc_resolution_t resolution)
{
    (void)config;
    (void)resolution;
}

uint32_t sx_adc_read(sx_adc_config_t *config)
{
    (void)config;
    return 0U;
}
#endif

/* ------------------------------------------------------------------------ */
/* Engine task                                                               */
/* ------------------------------------------------------------------------ */

static void splc_task(void *arg)
{
    (void)arg;
    TickType_t last_check = xTaskGetTickCount() - pdMS_TO_TICKS(SPLC_PORT_CHECK_MS);

    splc_mqtt_port_init();   /* publish callback; must precede plc_engine_init() */
    plc_engine_init();   /* board_hw_init() builds the virtual I/O from the port */
    /* Extra App link over LAN (Modbus TCP). Must follow plc_engine_init(): the library
     * contract for plc_modbus_cfg_add_transport(). No-op unless
     * SPLC_HOST_MODBUS_TCP_ENABLE; a failure is logged and USB/RS485 keep working. */
    (void)splc_modbus_tcp_port_start();

    for (;;) {
        if ((TickType_t)(xTaskGetTickCount() - last_check) >= pdMS_TO_TICKS(SPLC_PORT_CHECK_MS)) {
            last_check = xTaskGetTickCount();
            splc_check_port(&s_io_watch);
#if SPLC_HOST_APP_LINK_RS485
            splc_check_port(&s_app_watch);
#endif
        }
        plc_engine_poll();
        splc_mqtt_port_poll((uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS));
        // gw_rs485_diag_poll(GW_RS485_DIAG_PORT);   /* USART3 hardware test; remove when done */
        vTaskDelay(pdMS_TO_TICKS(1U));
    }
}

bool splc_host_start(void)
{
    static bool started;
    if (started) {
        log_error(TAG, "splc_host_start() called twice");
        return false;
    }

    const modbus_serial_port_t port = {
        .read     = rs485_read_cb,
        .write    = rs485_write_cb,
        .set_tx   = NULL,            /* rs485_send() drives DE itself */
        .ctx      = NULL,
        .baudrate = SPLC_HOST_RS485_BAUD,
    };
    if (!splc_gateway_set_rs485_port(&port)) {
        log_error(TAG, "RS485 port registration failed");
        return false;
    }

    const splc_link_ops_t ops = {
        .ctx     = NULL,
        .output  = app_link_output,
        .process = app_link_process,
        .kind    = MODBUS_TRANSPORT_KIND_RTU,
        .unit_id = (uint8_t)SPLC_MODBUS_UNIT_ID,
    };
    if (splc_link_register(&ops) != SPLC_LINK_OK) {
        log_error(TAG, "App link registration failed");
        return false;
    }

#if GW_USB_CDC_ENABLE
    /* CDC-only build: this port is the USB owner, so bring USB up here. */
    if (!gw_usb_cdc_start()) {
        log_error(TAG, "USB CDC start failed");
        splc_link_unregister();
        return false;
    }
#endif

    if (xTaskCreate(splc_task, "SPLC", (configSTACK_DEPTH_TYPE)SPLC_HOST_TASK_STACK_WORDS,
                    NULL, SPLC_HOST_TASK_PRIO, NULL) != pdPASS) {
        log_error(TAG, "engine task creation failed");
        splc_link_unregister();
        return false;
    }

    started = true;
    return true;
}