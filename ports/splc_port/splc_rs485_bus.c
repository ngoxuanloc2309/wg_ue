/*
 * splc_rs485_bus.c - see splc_rs485_bus.h.
 */
#include "splc_rs485_bus.h"
#include "splc_port.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "rs485_driver.h"
#include "modbus_serial.h"
#include "splc_log.h"
#if SPLC_MBM_ENABLE
#include "plc_mbm.h"
#endif

#define TAG "SPLC_BUS"

#define BUS_PORTS        RS485_MAX_PORTS      /* host UARTs: 0 USART3, 1 USART6, 2 USART10 */
#define SPLC_BUS_CHECK_MS 1000U
#define MBM_APP_PORTS     3U

/* Frame bytes at the line speed are 11 bits; the silence that ends an answer. */
static uint32_t silence_ms(uint32_t baud)
{
    return (baud != 0U ? (38500U / baud) : 4U) + 3U;
}

static uint32_t now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

/* Line settings in the host driver's encoding (rs485_runtime_config_t). */
typedef struct {
    uint32_t baud;
    uint8_t  data_bits;      /* 8 or 9: the driver counts the parity bit in the word length */
    uint8_t  parity;         /* 0 none, 1 odd, 2 even (driver) */
    uint8_t  stop_bits;
} line_t;

static bool line_eq(const line_t *a, const line_t *b)
{
    return a->baud == b->baud && a->data_bits == b->data_bits &&
           a->parity == b->parity && a->stop_bits == b->stop_bits;
}

typedef enum { OWN_FREE = 0, OWN_IO, OWN_MBM } owner_t;

typedef struct {
    bool     has_io;
    bool     has_mbm;
    int8_t   mbm_idx;          /* App port index (0..2) of the MB_READ/MB_WRITE port, -1 none */
    int8_t   problem;          /* last logged: -1 none yet, 0 ok, 1 owned by someone else, 2 not ready */
    line_t   io_line;
    line_t   applied;
    bool     applied_valid;
    owner_t  owner;
    bool     rx_seen;
    uint32_t hold_since;       /* start of the current transaction */
    uint32_t last_rx;
    uint32_t hold_limit_ms;    /* how long a transaction may wait for its first byte */
    uint32_t last_check;
    bool     checked_once;
} bus_t;

static bus_t s_bus[BUS_PORTS];

#define SHARED(b) ((b)->has_io && (b)->has_mbm)

/* ---- port ownership / line settings ------------------------------------- */

/* The port is ours (RS485_OWNER_MODBUS_GATEWAY) and RUNNING. Never blocks. */
static bool ensure_owned(uint8_t hp, bus_t *b)
{
    rs485_port_status_t st;
    int8_t problem = 0;

    if (rs485_port_get_status(hp, &st) != 0) {
        problem = 2;
    } else {
        if (st.owner == RS485_OWNER_NONE) {
            if (rs485_port_acquire(hp, RS485_OWNER_MODBUS_GATEWAY) != 0) {
                problem = 2;
            } else {
                st.owner = RS485_OWNER_MODBUS_GATEWAY;
            }
        }
        if (problem == 0 && st.owner != RS485_OWNER_MODBUS_GATEWAY) {
            problem = 1;
        }
        if (problem == 0 && st.state != RS485_STATE_RUNNING) {
            b->applied_valid = false;       /* the next apply must re-initialise */
            problem = 2;
        }
    }
    if (problem != b->problem) {
        if (problem == 1) {
            log_error(TAG, "RS485 port %d is owned by another host service; not served", (int)hp);
        } else if (problem == 2) {
            log_warn(TAG, "RS485 port %d not ready, retrying", (int)hp);
        } else if (b->problem > 0) {
            log_info(TAG, "RS485 port %d ready", (int)hp);
        }
        b->problem = problem;
    }
    return problem == 0;
}

static bool apply_line(uint8_t hp, bus_t *b, const line_t *l)
{
    if (b->applied_valid && line_eq(&b->applied, l)) {
        return true;
    }
    rs485_runtime_config_t cfg = {
        .baudrate = l->baud, .data_bits = l->data_bits, .parity = l->parity, .stop_bits = l->stop_bits,
    };
    if (rs485_port_reconfigure(hp, &cfg) != 0) {
        b->applied_valid = false;
        return false;
    }
    b->applied = *l;
    b->applied_valid = true;
    return true;
}

/* ---- shared bus ownership ---------------------------------------------- */

static void bus_release_if_done(bus_t *b, uint32_t now)
{
    if (b->owner == OWN_FREE) {
        return;
    }
    const uint32_t sil = silence_ms(b->applied_valid ? b->applied.baud : 9600U);
    const bool answered = b->rx_seen && (uint32_t)(now - b->last_rx) >= sil;
    const bool gave_up  = !b->rx_seen && (uint32_t)(now - b->hold_since) >= b->hold_limit_ms;
    if (answered || gave_up) {
        b->owner = OWN_FREE;
    }
}

/* ---- Remote I/O side ----------------------------------------------------- */

static bus_t *io_bus(void)
{
    return &s_bus[SPLC_HOST_RS485_PORT];
}

static int io_read_cb(void *ctx, uint8_t *buf, uint16_t max_len)
{
    (void)ctx;
    bus_t *b = io_bus();
    if (SHARED(b) && b->owner != OWN_IO) {
        return 0;                        /* the bytes belong to MB_READ/MB_WRITE */
    }
    int32_t n = rs485_recv_polling((uint8_t)SPLC_HOST_RS485_PORT, buf, max_len, 0U);
    if (n > 0) {
        b->rx_seen = true;
        b->last_rx = now_ms();
        return (int)n;
    }
    return 0;
}

/* rs485_send() drives DE and returns after the last bit left the UART (contract of
 * modbus_serial_port_t.write). timeout_ms is fixed inside the driver. */
static int io_write_cb(void *ctx, const uint8_t *buf, uint16_t len, uint32_t timeout_ms)
{
    (void)ctx;
    (void)timeout_ms;
    const uint8_t hp = (uint8_t)SPLC_HOST_RS485_PORT;
    bus_t *b = io_bus();
    const uint32_t now = now_ms();

    if (!ensure_owned(hp, b)) {
        return -1;
    }
    if (SHARED(b)) {
        bus_release_if_done(b, now);
        if (b->owner == OWN_MBM) {
            return -1;                   /* an MB_READ/MB_WRITE exchange is on the wire */
        }
    }
    if (!apply_line(hp, b, &b->io_line)) {
        return -1;
    }
    if (SHARED(b)) {
        rs485_flush(hp);                 /* leftovers of the previous owner's answer */
        b->owner = OWN_IO;
        b->rx_seen = false;
        b->hold_since = now;
        b->hold_limit_ms = SPLC_REMOTE_TIMEOUT_MS + 10U;
    }
    int32_t n = rs485_send(hp, buf, len);
    return (n == (int32_t)len) ? 0 : -1;
}

void splc_bus_io_port(modbus_serial_port_t *out)
{
    memset(out, 0, sizeof(*out));
    out->read     = io_read_cb;
    out->write    = io_write_cb;
    out->set_tx   = NULL;                /* rs485_send() drives DE itself */
    out->ctx      = NULL;
    out->baudrate = SPLC_HOST_RS485_BAUD;
}

/* ---- MB_READ / MB_WRITE side -------------------------------------------- */

#if SPLC_MBM_ENABLE

typedef struct {
    uint8_t        app_idx;      /* 0..2 = App port 1..3 */
    uint8_t        host_port;
    line_t         desired;      /* the App's line settings (default 9600 8N1) */
    const uint8_t *tx;           /* frame handed over by write_start(), valid until tx_done() */
    uint16_t       tx_len;
    bool           pending;      /* a frame waits for the bus */
} mbm_port_t;

static mbm_port_t s_mbm[MBM_APP_PORTS];

static int mbm_read_cb(void *ctx, uint8_t *buf, uint16_t max_len)
{
    mbm_port_t *p = (mbm_port_t *)ctx;
    bus_t *b = &s_bus[p->host_port];
    if (SHARED(b) && b->owner != OWN_MBM) {
        return 0;
    }
    int32_t n = rs485_recv_polling(p->host_port, buf, max_len, 0U);
    if (n > 0) {
        b->rx_seen = true;
        b->last_rx = now_ms();
        return (int)n;
    }
    return 0;
}

/* Non-blocking by contract: only remember the frame. tx_done() puts it on the wire as
 * soon as the bus is free; the master waits for it (and aborts after about twice the
 * frame time + 50 ms, which is err_code 1 for the block). */
static int mbm_write_start(void *ctx, const uint8_t *buf, uint16_t len)
{
    mbm_port_t *p = (mbm_port_t *)ctx;
    if (p->pending || len == 0U) {
        return -1;
    }
    p->tx = buf;
    p->tx_len = len;
    p->pending = true;
    return 0;
}

static bool mbm_tx_done(void *ctx)
{
    mbm_port_t *p = (mbm_port_t *)ctx;
    bus_t *b = &s_bus[p->host_port];
    const uint32_t now = now_ms();

    if (!p->pending) {
        return true;
    }
    if (!ensure_owned(p->host_port, b)) {
        return false;
    }
    if (SHARED(b)) {
        bus_release_if_done(b, now);
        if (b->owner != OWN_FREE) {
            return false;                /* the I/O boards (or a previous exchange) hold it */
        }
    }
    if (!apply_line(p->host_port, b, &p->desired)) {
        return false;
    }
    rs485_flush(p->host_port);
    if (SHARED(b)) {
        b->owner = OWN_MBM;
    }
    (void)rs485_send(p->host_port, p->tx, p->tx_len);   /* blocks for this one frame (TC) */
    b->rx_seen = false;
    b->hold_since = now_ms();
    b->hold_limit_ms = SPLC_HOST_BUS_MBM_HOLD_MS;
    p->pending = false;
    return true;                         /* even when rs485_send failed: the answer times out */
}

static void mbm_write_abort(void *ctx)
{
    ((mbm_port_t *)ctx)->pending = false;
}

/* App line settings (0x1400 table) -> host driver encoding. The driver counts the parity
 * bit in the word length: 8 data bits with parity = 9-bit word, 7 with parity = 8-bit word.
 * 7 data bits without parity does not exist in the driver. */
static int mbm_configure(void *ctx, const plc_mbm_serial_t *c)
{
    mbm_port_t *p = (mbm_port_t *)ctx;
    line_t l;
    l.baud = c->baud;
    l.stop_bits = (c->stop_bits == 2U) ? 2U : 1U;
    switch (c->parity) {
    case PLC_MBM_PARITY_EVEN: l.parity = 2U; break;
    case PLC_MBM_PARITY_ODD:  l.parity = 1U; break;
    default:                  l.parity = 0U; break;
    }
    if (c->data_bits == 8U) {
        l.data_bits = (l.parity != 0U) ? 9U : 8U;
    } else if (c->data_bits == 7U && l.parity != 0U) {
        l.data_bits = 8U;
    } else {
        log_error(TAG, "App port %u: %u data bits with parity %u is not supported by the host UART driver",
                  (unsigned)p->app_idx + 1U, (unsigned)c->data_bits, (unsigned)c->parity);
        return -1;
    }
    p->desired = l;                      /* applied when the bus is taken (tx_done) */
    return 0;
}

unsigned splc_bus_register_mbm(void)
{
    unsigned n = 0U;
    for (unsigned i = 0U; i < MBM_APP_PORTS; i++) {
        int hp = -1;
        for (unsigned h = 0U; h < BUS_PORTS; h++) {
            if (s_bus[h].mbm_idx == (int8_t)i) { hp = (int)h; }
        }
        if (hp < 0) {
            continue;
        }
        mbm_port_t *p = &s_mbm[i];
        p->app_idx = (uint8_t)i;
        p->host_port = (uint8_t)hp;
        p->desired = (line_t){ .baud = 9600U, .data_bits = 8U, .parity = 0U, .stop_bits = 1U };
        const modbus_serial_port_t port = {
            .read        = mbm_read_cb,
            .write       = NULL,
            .set_tx      = NULL,
            .ctx         = p,
            .baudrate    = 9600U,
            .write_start = mbm_write_start,
            .tx_done     = mbm_tx_done,
            .write_abort = mbm_write_abort,
        };
        splc_mbm_set_rs485_port((uint8_t)i, &port, mbm_configure);
        log_info(TAG, "App port %u = host RS485 %d (%s)", i + 1U, hp,
                 SHARED(&s_bus[hp]) ? "shared with the I/O boards" : "own bus");
        n++;
    }
    return n;
}

#endif /* SPLC_MBM_ENABLE */

/* ---- init / poll --------------------------------------------------------- */

void splc_bus_init(void)
{
    memset(s_bus, 0, sizeof(s_bus));
    for (unsigned h = 0U; h < BUS_PORTS; h++) {
        s_bus[h].mbm_idx = -1;
        s_bus[h].problem = -1;
    }
    bus_t *io = io_bus();
    io->has_io = true;
    io->io_line = (line_t){ .baud = SPLC_HOST_RS485_BAUD, .data_bits = 8U,
                            .parity = (uint8_t)SPLC_HOST_RS485_PARITY, .stop_bits = 1U };
#if SPLC_MBM_ENABLE
    const int wired[MBM_APP_PORTS] = { SPLC_HOST_MBM_PORT0, SPLC_HOST_MBM_PORT1, SPLC_HOST_MBM_PORT2 };
    for (unsigned i = 0U; i < MBM_APP_PORTS; i++) {
        if (wired[i] < 0) {
            continue;
        }
        bus_t *b = &s_bus[wired[i]];
        if (b->has_io && !SPLC_HOST_MBM_SHARE_IO_BUS) {
            log_warn(TAG, "App port %u is the I/O boards UART and sharing is off: not registered",
                     i + 1U);
            continue;
        }
        b->has_mbm = true;
        b->mbm_idx = (int8_t)i;
    }
#endif
}

void splc_bus_poll(uint32_t now)
{
    for (unsigned h = 0U; h < BUS_PORTS; h++) {
        bus_t *b = &s_bus[h];
        if (!b->has_io && !b->has_mbm) {
            continue;
        }
        if (SHARED(b)) {
            bus_release_if_done(b, now);
        }
        if (b->checked_once && (uint32_t)(now - b->last_check) < SPLC_BUS_CHECK_MS) {
            continue;
        }
        b->checked_once = true;
        b->last_check = now;
        /* Keep the port ours; the I/O boards need it ready (and at their speed) before
         * their first request. A pure MB_READ/MB_WRITE port is configured on first use. */
        if (ensure_owned((uint8_t)h, b) && b->has_io && b->owner != OWN_MBM) {
            (void)apply_line((uint8_t)h, b, &b->io_line);
        }
    }
}