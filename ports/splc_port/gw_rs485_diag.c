/*
 * gw_rs485_diag.c - temporary RS485 bring-up diagnostics for ONE host port (see .h).
 *
 * Default target: RS485 port 0 = USART3 (RX PD9, DE PD10). Everything is log output
 * (tag RS485_DIAG) plus, on the wire:
 *   - a "hello #n" frame every GW_RS485_DIAG_TX_PERIOD_MS (checks TX, DE polarity, A/B),
 *   - an echo of every frame the PC sends (checks RX).
 *
 * The module takes ownership of the port only when nobody owns it, and only touches the
 * UART configuration (rs485_port_reconfigure) of a port it owns. If another service
 * already owns the port it stays in monitor mode: counters and pin levels are logged,
 * but it neither reads the RX stream nor transmits.
 *
 * Bytes the UART flags as errors are dropped by the host driver, so a byte that is
 * missing from "Rx=" was corrupted on the wire (see hw_err in the status line).
 */
#include "gw_rs485_diag.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "rs485_driver.h"
#include "splc_log.h"
#include "splcopts.h"

/* ------------------------------------------------------------------------ */
/* Options (override with -D or define before including this file)           */
/* ------------------------------------------------------------------------ */

/* Serial settings of the test, 8 data bits, no parity, 1 stop bit. Set the PC
 * USB-RS485 adapter to the same. */
#ifndef GW_RS485_DIAG_BAUD
#define GW_RS485_DIAG_BAUD          115200U
#endif

/* 1 = send "hello #n\r\n" every GW_RS485_DIAG_TX_PERIOD_MS. */
#ifndef GW_RS485_DIAG_TX_TEST
#define GW_RS485_DIAG_TX_TEST       1
#endif
#ifndef GW_RS485_DIAG_TX_PERIOD_MS
#define GW_RS485_DIAG_TX_PERIOD_MS  2000U
#endif

/* 1 = send back every received frame unchanged (a frame ends after
 * GW_RS485_DIAG_RX_IDLE_MS of silence). */
#ifndef GW_RS485_DIAG_ECHO
#define GW_RS485_DIAG_ECHO          1
#endif

/* Bring-up only: software logic analyzer on the RX pin (records level runs in us).
 *
 * GW_RS485_DIAG_CAPTURE    = 1: after each TX test, wait (max GW_RS485_DIAG_CAP_WAIT_MS) for a
 *                              falling edge, then record. BLOCKS the SPLC task while waiting.
 * GW_RS485_DIAG_CAPTURE_RX = 1: shows what the UART sees when the PC sends. Every pass it
 *                              spins up to GW_RS485_DIAG_CAP_RX_SPIN_US on the RX pin; on a
 *                              falling edge it records GW_RS485_DIAG_CAP_RX_US with interrupts
 *                              ENABLED (so USART6 / other ISRs are not starved; an ISR may hide
 *                              a pulse shorter than ~10 us). Costs CPU: bring-up only.
 *                              Tip: send the same frame from the PC several times, the capture
 *                              catches ~1 frame in 4. 1 bit = 1e6 / baud us.
 * CAPTURE is off by default, CAPTURE_RX is on while the USART3 RX path is being debugged. */
#ifndef GW_RS485_DIAG_CAPTURE
#define GW_RS485_DIAG_CAPTURE       0
#endif
#ifndef GW_RS485_DIAG_CAPTURE_RX
#define GW_RS485_DIAG_CAPTURE_RX    1   /* ON while hunting the RX framing errors; set 0 afterwards */
#endif
#define GW_RS485_DIAG_CAP_WAIT_MS   1500U
#define GW_RS485_DIAG_CAP_US        2000U   /* TX-test capture window after the trigger */
#define GW_RS485_DIAG_CAP_RX_SPIN_US 300U   /* RX capture: edge wait per pass           */
#define GW_RS485_DIAG_CAP_RX_US     1200U   /* RX capture window after the trigger      */
#define GW_RS485_DIAG_CAP_MAX_RUNS  96U

#define GW_RS485_DIAG_HEARTBEAT_MS  5000U
#define GW_RS485_DIAG_RX_KEEP       16U    /* bytes kept per captured frame      */
#define GW_RS485_DIAG_RX_IDLE_MS    5U     /* quiet time that ends a frame       */
#define GW_RS485_DIAG_ECHO_BUF      128U
#define GW_RS485_DIAG_RETRY_MS      1000U  /* retry of the port preparation      */

/* Owner the diagnostics register on the host driver. */
#define GW_RS485_DIAG_OWNER         RS485_OWNER_MODBUS_GATEWAY

/* ------------------------------------------------------------------------ */
/* Compile-time checks                                                       */
/* ------------------------------------------------------------------------ */

#if (GW_RS485_DIAG_PORT) > 2
#error "GW_RS485_DIAG_PORT must be 0 (USART3), 1 (USART6) or 2 (USART10)"
#endif
#if defined(SPLC_HOST_RS485_PORT) && ((GW_RS485_DIAG_PORT) == (SPLC_HOST_RS485_PORT))
#error "GW_RS485_DIAG_PORT is the SimplePLC I/O bus port (SPLC_HOST_RS485_PORT): pick another port"
#endif
#if defined(SPLC_HOST_APP_RS485_PORT) && ((GW_RS485_DIAG_PORT) == (SPLC_HOST_APP_RS485_PORT))
#error "GW_RS485_DIAG_PORT is the SimplePLC App port (SPLC_HOST_APP_RS485_PORT): pick another port"
#endif

static const char *TAG = "RS485_DIAG";

/* ------------------------------------------------------------------------ */
/* Port hardware table                                                       */
/* ------------------------------------------------------------------------ */

/* The DE pins come from the host driver (CubeMX defines in main.h). The RX pins are not
 * defined there; they follow Documents/Hardware/02_PINOUT_AND_INTERFACES.md section 3.4
 * and are read only as a plain input level (IDR works while the pin is in AF mode). */
typedef struct {
    GPIO_TypeDef *de_port;
    uint16_t      de_pin;
    GPIO_TypeDef *rx_port;
    uint16_t      rx_pin;
    const char   *name;
} gw_rs485_diag_hw_t;

static const gw_rs485_diag_hw_t s_hw[RS485_MAX_PORTS] = {
    { RS485_PORT0_DE_PORT, RS485_PORT0_DE_PIN, GPIOD, GPIO_PIN_9, "USART3"  },
    { RS485_PORT1_DE_PORT, RS485_PORT1_DE_PIN, GPIOC, GPIO_PIN_7, "USART6"  },
    { RS485_PORT2_DE_PORT, RS485_PORT2_DE_PIN, GPIOE, GPIO_PIN_4, "USART10" },
};

static inline uint8_t gw_rs485_diag_rx_level(const gw_rs485_diag_hw_t *hw)
{
    return (HAL_GPIO_ReadPin(hw->rx_port, hw->rx_pin) == GPIO_PIN_SET) ? 1U : 0U;
}

/* ------------------------------------------------------------------------ */
/* Received-frame capture                                                    */
/* ------------------------------------------------------------------------ */

static char     s_last_rx[GW_RS485_DIAG_RX_KEEP * 3U + 1U] = "-";
static uint16_t s_last_rx_len;       /* total bytes of the last frame           */
static uint32_t s_frame_count;       /* completed frames, drives the log line   */

void gw_rs485_diag_rx(const uint8_t *data, uint16_t len)
{
    static const char hexchars[] = "0123456789ABCDEF";
    static uint8_t  keep[GW_RS485_DIAG_RX_KEEP];
    static uint16_t stored;          /* bytes held in keep[]            */
    static uint16_t seen;            /* bytes seen, may exceed stored   */
    static uint32_t last_byte_ms;

    const uint32_t now = HAL_GetTick();

    if (data != NULL && len > 0U) {
        for (uint16_t i = 0U; i < len; i++) {
            if (stored < GW_RS485_DIAG_RX_KEEP) {
                keep[stored++] = data[i];
            }
            if (seen < UINT16_MAX) {
                seen++;
            }
        }
        last_byte_ms = now;
        return;
    }

    if (seen == 0U || (uint32_t)(now - last_byte_ms) < GW_RS485_DIAG_RX_IDLE_MS) {
        return;
    }

    uint16_t pos = 0U;
    for (uint16_t i = 0U; i < stored; i++) {
        s_last_rx[pos++] = hexchars[keep[i] >> 4];
        s_last_rx[pos++] = hexchars[keep[i] & 0x0FU];
        s_last_rx[pos++] = ' ';
    }
    if (seen > stored && pos > 0U) {
        s_last_rx[pos - 1U] = '+';   /* more bytes followed than are shown */
    }
    s_last_rx[pos] = '\0';
    s_last_rx_len = seen;
    s_frame_count++;
    stored = 0U;
    seen = 0U;
}

/* ------------------------------------------------------------------------ */
/* Optional RX pin logic analyzer                                            */
/* ------------------------------------------------------------------------ */

#if GW_RS485_DIAG_CAPTURE || GW_RS485_DIAG_CAPTURE_RX
/* Records level runs of the RX pin (us) after a falling edge seen within wait_us. Returns the
 * number of runs captured (0 = no edge in time). out_us[0] is the first LOW run. irq_off = 1
 * records with interrupts disabled (exact, but starves every other ISR for window_us). */
static uint16_t gw_rs485_diag_capture(const gw_rs485_diag_hw_t *hw, uint16_t *out_us,
                                      uint16_t max_runs, uint8_t *overflow,
                                      uint32_t wait_us, uint32_t window_us, bool irq_off)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;     /* do NOT reset CYCCNT: host uses it */
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    const uint32_t cyc_per_us = SystemCoreClock / 1000000U;
    const uint32_t window_cyc = window_us * cyc_per_us;
    const uint32_t wait_cyc   = wait_us * cyc_per_us;
    const uint32_t wait_start = DWT->CYCCNT;

    *overflow = 0U;

    /* wait for a high -> low edge (polling); a pin that is already low counts as the edge */
    uint8_t prev = gw_rs485_diag_rx_level(hw);
    if (prev) {
        for (;;) {
            const uint8_t cur = gw_rs485_diag_rx_level(hw);
            if (!cur) {
                break;
            }
            if ((uint32_t)(DWT->CYCCNT - wait_start) >= wait_cyc) {
                return 0U;
            }
        }
    }

    uint16_t n = 0U;
    uint8_t  level = 0U;                                 /* we are in the first LOW run */
    if (irq_off) { __disable_irq(); }
    uint32_t t0 = DWT->CYCCNT;                           /* start of current run        */
    const uint32_t tstart = t0;
    for (;;) {
        const uint32_t now = DWT->CYCCNT;
        const uint8_t  cur = gw_rs485_diag_rx_level(hw);
        if (cur != level) {
            const uint32_t us = (now - t0) / cyc_per_us;
            if (n < max_runs) {
                out_us[n++] = (uint16_t)((us > 65535U) ? 65535U : us);
            } else {
                *overflow = 1U;
                break;
            }
            level = cur;
            t0 = now;
        }
        if ((uint32_t)(now - tstart) >= window_cyc) {
            const uint32_t us = (now - t0) / cyc_per_us;     /* last (open) run */
            if (n < max_runs) {
                out_us[n++] = (uint16_t)((us > 65535U) ? 65535U : us);
            }
            break;
        }
    }
    if (irq_off) { __enable_irq(); }
    return n;
}

/* Logs runs as "L87/10b": level, length in us / length in bit times at GW_RS485_DIAG_BAUD.
 * A clean 0x55 frame is a train of "1b" runs (9 us at 115200); one long L run = break/stuck. */
static void gw_rs485_diag_capture_log(const gw_rs485_diag_hw_t *hw, const char *what,
                                      uint32_t wait_us, uint32_t window_us, bool irq_off)
{
    static uint16_t runs[GW_RS485_DIAG_CAP_MAX_RUNS];
    uint8_t overflow;
    const uint16_t n = gw_rs485_diag_capture(hw, runs, GW_RS485_DIAG_CAP_MAX_RUNS, &overflow,
                                             wait_us, window_us, irq_off);

    if (n == 0U) {
        if (wait_us >= 1000U) {
            log_info(TAG, "CAP(%s): no falling edge on the %s RX pin within %lu us",
                     what, hw->name, (unsigned long)wait_us);
        }
        return;
    }
    log_info(TAG, "CAP(%s): %u runs in %lu us window%s (L/H = pin level; us / bit times at %lu baud, 1 bit = %lu us)",
             what, (unsigned)n, (unsigned long)window_us, overflow ? " [run buffer full]" : "",
             (unsigned long)GW_RS485_DIAG_BAUD, (unsigned long)(1000000UL / GW_RS485_DIAG_BAUD));

    for (uint16_t i = 0U; i < n; i += 10U) {
        char line[10U * 14U + 1U];
        uint16_t pos = 0U;
        for (uint16_t k = i; k < n && k < (uint16_t)(i + 10U); k++) {
            const uint32_t bits = ((uint32_t)runs[k] * GW_RS485_DIAG_BAUD + 500000UL) / 1000000UL;
            const int w = snprintf(&line[pos], sizeof line - pos, "%c%u/%lub ",
                                   (k & 1U) ? 'H' : 'L', (unsigned)runs[k], (unsigned long)bits);
            if (w < 0 || (uint16_t)w >= (uint16_t)(sizeof line - pos)) { break; }
            pos += (uint16_t)w;
        }
        line[pos] = '\0';
        log_info(TAG, "CAP[%u]: %s", (unsigned)i, line);
    }
}
#endif /* GW_RS485_DIAG_CAPTURE || GW_RS485_DIAG_CAPTURE_RX */

/* ------------------------------------------------------------------------ */
/* Diagnostic state                                                          */
/* ------------------------------------------------------------------------ */

static bool     s_ready;             /* port prepared (owned or monitor-only)   */
static bool     s_we_own;            /* false = monitor only                    */
static uint32_t s_last_prepare_ms;
static uint32_t s_prepare_fails;
static uint32_t s_err_base;          /* hw_errors when the diagnostics started  */

static uint32_t s_rx_total;          /* bytes read from the stream here         */
static uint32_t s_tx_sent;           /* "hello" frames sent OK                  */
static uint32_t s_tx_failed;         /* "hello" frames that failed              */
static uint32_t s_echo_frames;       /* frames echoed back                      */
static uint32_t s_echo_failed;

static uint8_t  s_echo_buf[GW_RS485_DIAG_ECHO_BUF];
static uint16_t s_echo_len;
static uint32_t s_echo_last_byte_ms;

/* Window statistics of the RX pin, restarted at each heartbeat. */
static uint32_t s_pin_lo, s_pin_hi, s_pin_edges;
static int8_t   s_pin_prev = -1;

/* Give the port a known state: owned by us (or monitor only) and running our settings.
 * Like splc_check_port() of the SimplePLC ports, this also recovers a port that the host
 * left in RS485_STATE_ERROR / STOPPED (e.g. modbus_master_init() applied baudrate 0 from an
 * empty modbus config and HAL_UART_Init() failed). Returns false while the host driver is
 * not initialised yet (retried by the caller). */
static bool gw_rs485_diag_prepare(uint8_t port, const gw_rs485_diag_hw_t *hw)
{
    rs485_port_status_t st;

    if (rs485_port_get_status(port, &st) != 0) {
        return false;
    }

    if (st.owner == RS485_OWNER_NONE) {
        if (rs485_port_acquire(port, GW_RS485_DIAG_OWNER) != 0) {
            if ((s_prepare_fails % 10U) == 0U) {
                log_warn(TAG, "%s: rs485_port_acquire() failed (rs485_port_init not done yet?)", hw->name);
            }
            s_prepare_fails++;
            return false;
        }
        s_we_own = true;
    } else if (st.owner == GW_RS485_DIAG_OWNER) {
        s_we_own = true;
    } else {
        s_we_own = false;
        log_warn(TAG, "%s is owned by service %d (state=%d): MONITOR ONLY (no read, no TX, no echo). "
                      "Free the port (disable it in the Modbus config) to run the TX/RX test",
                 hw->name, (int)st.owner, (int)st.state);
    }

    if (s_we_own) {
        if (st.state != RS485_STATE_RUNNING || st.config.baudrate != GW_RS485_DIAG_BAUD ||
            st.config.data_bits != 8U || st.config.parity != 0U || st.config.stop_bits != 1U) {
            const rs485_runtime_config_t cfg = {
                .baudrate  = GW_RS485_DIAG_BAUD,
                .data_bits = 8U,
                .parity    = 0U,
                .stop_bits = 1U,
            };
            log_info(TAG, "%s: state=%d baud=%lu data=%u par=%u stop=%u -> applying %lu 8N1",
                     hw->name, (int)st.state, (unsigned long)st.config.baudrate,
                     (unsigned)st.config.data_bits, (unsigned)st.config.parity,
                     (unsigned)st.config.stop_bits, (unsigned long)GW_RS485_DIAG_BAUD);
            if (rs485_port_reconfigure(port, &cfg) != 0) {
                log_error(TAG, "%s: rs485_port_reconfigure(%lu 8N1) failed (HAL_UART_Init error?)",
                          hw->name, (unsigned long)GW_RS485_DIAG_BAUD);
                (void)rs485_port_release(port, GW_RS485_DIAG_OWNER);
                s_we_own = false;
                s_prepare_fails++;
                return false;
            }
        }
        rs485_flush(port);
    }

    (void)rs485_port_get_status(port, &st);
    s_err_base = st.counters.hw_errors;
    log_info(TAG, "%s (port %u) ready: %s, state=%d, %lu 8N1, DE pin level=%u, RX pin level=%u (idle must be 1)",
             hw->name, (unsigned)port, s_we_own ? "owned by diag" : "monitor only", (int)st.state,
             (unsigned long)st.config.baudrate,
             (unsigned)((HAL_GPIO_ReadPin(hw->de_port, hw->de_pin) == GPIO_PIN_SET) ? 1U : 0U),
             (unsigned)gw_rs485_diag_rx_level(hw));
    log_info(TAG, "PC side: open the USB-RS485 adapter at %lu 8N1; expect \"hello #n\" every %u ms%s",
             (unsigned long)GW_RS485_DIAG_BAUD, (unsigned)GW_RS485_DIAG_TX_PERIOD_MS,
             GW_RS485_DIAG_ECHO ? "; anything you type is echoed back" : "");
    return true;
}

/* Flush the echo buffer to the wire. */
static void gw_rs485_diag_echo_flush(uint8_t port)
{
    if (s_echo_len == 0U) {
        return;
    }
#if GW_RS485_DIAG_ECHO
    const int32_t r = rs485_send(port, s_echo_buf, s_echo_len);
    if (r == (int32_t)s_echo_len) {
        s_echo_frames++;
    } else {
        s_echo_failed++;
        log_warn(TAG, "echo: rs485_send(%u bytes) = %ld", (unsigned)s_echo_len, (long)r);
    }
#else
    (void)port;
#endif
    s_echo_len = 0U;
}

static const char *gw_rs485_diag_pin_state(void)
{
    if (s_pin_lo + s_pin_hi == 0U) { return "n/a"; }
    if (s_pin_hi == 0U)            { return "STUCK-LOW"; }
    if (s_pin_lo == 0U)            { return "idle-high"; }
    return "active";
}

/* ------------------------------------------------------------------------ */
/* Poll                                                                      */
/* ------------------------------------------------------------------------ */

void gw_rs485_diag_poll(uint8_t port)
{
    static bool     have_prev;
    static uint32_t prev_tx, prev_err, prev_ovf, prev_frames;
    static uint8_t  prev_de;
    static uint32_t last_log_ms;

    if (port != (uint8_t)GW_RS485_DIAG_PORT) {
        return;
    }
    const gw_rs485_diag_hw_t *hw = &s_hw[port];
    const uint32_t now = HAL_GetTick();

    if (!s_ready) {
        if (s_prepare_fails != 0U && (uint32_t)(now - s_last_prepare_ms) < GW_RS485_DIAG_RETRY_MS) {
            return;
        }
        s_last_prepare_ms = now;
        s_ready = gw_rs485_diag_prepare(port, hw);
        if (!s_ready) {
            if (s_prepare_fails == 0U) {
                s_prepare_fails = 1U;       /* start the retry timer */
            }
            return;
        }
    }

    /* ---- RX: read the stream, log frames, collect them for the echo ---- */
    if (s_we_own) {
        uint8_t tmp[64];
        const int32_t n = rs485_recv_polling(port, tmp, (uint16_t)sizeof tmp, 0U);

        if (n > 0) {
            gw_rs485_diag_rx(tmp, (uint16_t)n);
            s_rx_total += (uint32_t)n;
            for (int32_t i = 0; i < n; i++) {
                if (s_echo_len >= GW_RS485_DIAG_ECHO_BUF) {
                    gw_rs485_diag_echo_flush(port);
                }
                s_echo_buf[s_echo_len++] = tmp[i];
            }
            s_echo_last_byte_ms = now;
        } else {
            gw_rs485_diag_rx(NULL, 0U);
            if (s_echo_len > 0U && (uint32_t)(now - s_echo_last_byte_ms) >= GW_RS485_DIAG_RX_IDLE_MS) {
                gw_rs485_diag_echo_flush(port);
            }
        }
    }

#if GW_RS485_DIAG_CAPTURE_RX
    if (s_we_own) {
        gw_rs485_diag_capture_log(hw, "PC->MCU", GW_RS485_DIAG_CAP_RX_SPIN_US,
                                  GW_RS485_DIAG_CAP_RX_US, false);
    }
#endif

    rs485_port_status_t st;
    if (rs485_port_get_status(port, &st) != 0) {
        return;
    }

    const uint8_t  de  = (HAL_GPIO_ReadPin(hw->de_port, hw->de_pin) == GPIO_PIN_SET) ? 1U : 0U;
    const uint32_t tx  = st.counters.tx_bytes;
    const uint32_t err = st.counters.hw_errors;
    const uint32_t ovf = st.counters.overflows;

    /* Sample the RX pin level every pass: tells a stuck RO from an idle-high / toggling one. */
    {
        const int8_t lvl = (int8_t)gw_rs485_diag_rx_level(hw);
        if (lvl) { s_pin_hi++; } else { s_pin_lo++; }
        if (s_pin_prev >= 0 && lvl != s_pin_prev) { s_pin_edges++; }
        s_pin_prev = lvl;
    }

#if GW_RS485_DIAG_TX_TEST
    if (s_we_own) {
        static uint32_t last_tx_ms;
        static uint32_t seq;
        static bool     tx_started;

        if (!tx_started || (uint32_t)(now - last_tx_ms) >= GW_RS485_DIAG_TX_PERIOD_MS) {
            char msg[24];
            const int len = snprintf(msg, sizeof msg, "hello #%lu\r\n", (unsigned long)seq);

            tx_started = true;
            last_tx_ms = now;
            if (len > 0) {
                const uint32_t tx_before = tx;
                /* blocking send (~1 ms at 115200); drives DE and waits for TC */
                const int32_t r = rs485_send(port, (const uint8_t *)msg, (uint16_t)len);
                const uint8_t de_after =
                    (HAL_GPIO_ReadPin(hw->de_port, hw->de_pin) == GPIO_PIN_SET) ? 1U : 0U;
                rs485_port_status_t st2;
                const uint32_t tx_delta =
                    (rs485_port_get_status(port, &st2) == 0) ? (st2.counters.tx_bytes - tx_before) : 0U;
                const bool ok = (r == (int32_t)len) && (de_after == 0U) && (tx_delta == (uint32_t)len);

                if (ok) { s_tx_sent++; } else { s_tx_failed++; }
                seq++;
                log_info(TAG, "TX test %s: rs485_send(\"hello #%lu\") = %ld (expect %d), tx_bytes +%lu, "
                              "DE after send=%u (expect 0) -> PC must show it",
                         ok ? "PASS" : "FAIL", (unsigned long)(seq - 1U), (long)r, len,
                         (unsigned long)tx_delta, (unsigned)de_after);
#if GW_RS485_DIAG_CAPTURE
                gw_rs485_diag_capture_log(hw, "after TX", GW_RS485_DIAG_CAP_WAIT_MS * 1000U,
                                          GW_RS485_DIAG_CAP_US, true);
#endif
            }
        }
    }
#endif /* GW_RS485_DIAG_TX_TEST */

    const bool changed = !have_prev || tx != prev_tx || err != prev_err || ovf != prev_ovf ||
                         de != prev_de || s_frame_count != prev_frames;
    const bool heartbeat = (uint32_t)(now - last_log_ms) >= GW_RS485_DIAG_HEARTBEAT_MS;

    if (changed || heartbeat) {
        const uint32_t new_err = err - s_err_base;
        const char *rx_verdict =
            (new_err != 0U && new_err >= s_rx_total) ? "ALL-BYTES-BAD(framing/baud/RO line)" :
            (new_err != 0U)  ? "ERRORS(some bytes bad)" :
            (s_rx_total == 0U) ? "WAIT(no data from PC yet)" : "PASS";
        const char *tx_verdict =
            (s_tx_failed != 0U) ? "FAIL" : ((s_tx_sent != 0U) ? "PASS" : "n/a");

        log_info(TAG,
                 "%s port=%u owner=%d state=%d %lu baud | rx_bytes=%lu tx_bytes=%lu "
                 "overflows=%lu hw_err=%lu last_err=0x%lx | DE=%u | "
                 "TX=%s (ok %lu fail %lu) RX=%s (%lu B) echo=%lu/%lu fail | "
                 "RXpin=%s lo=%lu hi=%lu edges=%lu | Rx[%u]=%s",
                 hw->name, (unsigned)port, (int)st.owner, (int)st.state,
                 (unsigned long)st.config.baudrate,
                 (unsigned long)st.counters.rx_bytes, (unsigned long)tx,
                 (unsigned long)ovf, (unsigned long)new_err,
                 (unsigned long)st.counters.last_error_code, (unsigned)de,
                 tx_verdict, (unsigned long)s_tx_sent, (unsigned long)s_tx_failed,
                 rx_verdict, (unsigned long)s_rx_total,
                 (unsigned long)s_echo_frames, (unsigned long)s_echo_failed,
                 gw_rs485_diag_pin_state(), (unsigned long)s_pin_lo,
                 (unsigned long)s_pin_hi, (unsigned long)s_pin_edges,
                 (unsigned)s_last_rx_len, s_last_rx);
        if (heartbeat) {
            last_log_ms = now;
            s_pin_lo = 0U; s_pin_hi = 0U; s_pin_edges = 0U;   /* new window */
        }
    }

    prev_tx = tx; prev_err = err; prev_ovf = ovf; prev_de = de;
    prev_frames = s_frame_count;
    have_prev = true;
}