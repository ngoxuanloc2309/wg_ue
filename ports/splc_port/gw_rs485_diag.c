/*
 * gw_rs485_diag.c - temporary RS485 bring-up diagnostics (log only).
 *
 * Reads the host driver's counters through rs485_port_get_status() and the DE/RE
 * pin through HAL_GPIO_ReadPin(); it never writes a pin or touches the UART
 * configuration. In drain mode it also consumes the port's RX stream (see .h).
 *
 * Bytes the UART flags as errors are dropped by the host driver, so a byte that
 * is missing from "Rx=" was corrupted on the wire.
 */
#include "gw_rs485_diag.h"

#include <stdbool.h>

#include "rs485_driver.h"
#include "splc_log.h"

#ifndef GW_RS485_DIAG_DRAIN
#define GW_RS485_DIAG_DRAIN         1      /* 1 = read the RX stream here        */
#endif
#ifndef GW_RS485_DIAG_PORT
#define GW_RS485_DIAG_PORT          1U     /* only used by the --wrap hook       */
#endif

#define GW_RS485_DIAG_HEARTBEAT_MS  5000U
#define GW_RS485_DIAG_RX_KEEP       16U    /* bytes kept per captured frame      */
#define GW_RS485_DIAG_RX_IDLE_MS    5U     /* quiet time that ends a frame       */

static const char *TAG = "RS485_DIAG";

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
/* Status line                                                               */
/* ------------------------------------------------------------------------ */

typedef struct {
    GPIO_TypeDef *de_port;
    uint16_t      de_pin;
    const char   *name;
} gw_rs485_diag_hw_t;

static bool gw_rs485_diag_hw(uint8_t port, gw_rs485_diag_hw_t *hw)
{
    switch (port) {
    case 0U:
        hw->de_port = RS485_PORT0_DE_PORT; hw->de_pin = RS485_PORT0_DE_PIN;
        hw->name = "USART3";  return true;
    case 1U:
        hw->de_port = RS485_PORT1_DE_PORT; hw->de_pin = RS485_PORT1_DE_PIN;
        hw->name = "USART6";  return true;
    case 2U:
        hw->de_port = RS485_PORT2_DE_PORT; hw->de_pin = RS485_PORT2_DE_PIN;
        hw->name = "USART10"; return true;
    default:
        return false;
    }
}

void gw_rs485_diag_poll(uint8_t port)
{
    static bool     have_prev;
    static uint32_t prev_rx, prev_tx, prev_err, prev_ovf, prev_frames;
    static uint8_t  prev_de;
    static uint32_t last_log_ms;

    gw_rs485_diag_hw_t hw;
    rs485_port_status_t st;

    if (!gw_rs485_diag_hw(port, &hw)) {
        return;
    }

#if GW_RS485_DIAG_DRAIN
    {
        uint8_t tmp[64];
        const int32_t n = rs485_recv_polling(port, tmp, (uint16_t)sizeof tmp, 0U);
        gw_rs485_diag_rx(tmp, (n > 0) ? (uint16_t)n : 0U);
    }
#endif

    if (rs485_port_get_status(port, &st) != 0) {
        return;
    }

    const uint8_t  de  = (HAL_GPIO_ReadPin(hw.de_port, hw.de_pin) == GPIO_PIN_SET) ? 1U : 0U;
    const uint32_t now = HAL_GetTick();
    const uint32_t rx  = st.counters.rx_bytes;
    const uint32_t tx  = st.counters.tx_bytes;
    const uint32_t err = st.counters.hw_errors;
    const uint32_t ovf = st.counters.overflows;

    const bool changed = !have_prev || rx != prev_rx || tx != prev_tx ||
                         err != prev_err || ovf != prev_ovf || de != prev_de ||
                         s_frame_count != prev_frames;
    const bool heartbeat = (uint32_t)(now - last_log_ms) >= GW_RS485_DIAG_HEARTBEAT_MS;

    if (changed || heartbeat) {
        log_info(TAG,
                 "%s port=%u state=%d owner=%d %lu baud | "
                 "rx_bytes=%lu (+%lu) tx_bytes=%lu (+%lu) rx_frames=%lu overflows=%lu "
                 "hw_err=%lu last_err=0x%lx | DE=%u | Rx[%u]=%s",
                 hw.name, (unsigned)port, (int)st.state, (int)st.owner,
                 (unsigned long)st.config.baudrate,
                 (unsigned long)rx, (unsigned long)(rx - prev_rx),
                 (unsigned long)tx, (unsigned long)(tx - prev_tx),
                 (unsigned long)st.counters.rx_frames,
                 (unsigned long)ovf, (unsigned long)err,
                 (unsigned long)st.counters.last_error_code, (unsigned)de,
                 (unsigned)s_last_rx_len, s_last_rx);
        last_log_ms = now;
    }

    prev_rx = rx; prev_tx = tx; prev_err = err; prev_ovf = ovf; prev_de = de;
    prev_frames = s_frame_count;
    have_prev = true;
}

/* ------------------------------------------------------------------------ */
/* Optional: capture without draining (linker --wrap)                        */
/* ------------------------------------------------------------------------ */
/*
 * Use with GW_RS485_DIAG_DRAIN=0 and GW_RS485_DIAG_WRAP=1, and link the
 * executable with -Wl,--wrap=rs485_recv_polling. SimplePLC then keeps receiving
 * normally while the bytes it reads are also captured here.
 */
#if defined(GW_RS485_DIAG_WRAP) && (GW_RS485_DIAG_WRAP == 1)

int32_t __real_rs485_recv_polling(uint8_t port, uint8_t *data, uint16_t max_len,
                                  uint32_t timeout_ms);

int32_t __wrap_rs485_recv_polling(uint8_t port, uint8_t *data, uint16_t max_len,
                                  uint32_t timeout_ms)
{
    const int32_t n = __real_rs485_recv_polling(port, data, max_len, timeout_ms);

    if (port == (uint8_t)GW_RS485_DIAG_PORT) {
        gw_rs485_diag_rx(data, (n > 0) ? (uint16_t)n : 0U);
    }
    return n;
}

#endif /* GW_RS485_DIAG_WRAP */