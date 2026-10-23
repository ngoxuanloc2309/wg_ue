/*
 * splc_modbus_tcp_port.c - Modbus TCP transport for SimplePLC (see the header).
 *
 * Every function here runs in the engine task (SimplePLC contract), so the
 * state below needs no lock. Sockets are lwIP BSD sockets used non-blocking:
 *   - listen socket: O_NONBLOCK, accept() polled from process();
 *   - client socket: recv(MSG_DONTWAIT) for "is there a request", SO_RCVTIMEO
 *     only for the short inter-byte wait inside a frame that has started.
 */
#include "splc_modbus_tcp_port.h"

#if SPLC_HOST_MODBUS_TCP_ENABLE

#include <stdint.h>
#include <stdbool.h>

#include "FreeRTOS.h"
#include "task.h"
#include "lwip/sockets.h"
#include "lwip/ip4_addr.h"
#include "network_manager.h"     /* network_get_active_interface() */
#include "app_config.h"          /* EDGE_DEFAULT_MODBUS_TCP_PORT */
#include "modbus_transport.h"
#include "plc_modbus_cfg.h"
#include "splc_log.h"

_Static_assert(SPLC_HOST_MODBUS_TCP_PORT != EDGE_DEFAULT_MODBUS_TCP_PORT,
               "SPLC_HOST_MODBUS_TCP_PORT collides with apps/modbus_tcp_server");

static const char *TAG = "SPLC_MBTCP";

#define LISTEN_RETRY_MS     1000U
#define LISTEN_BACKLOG      1

static int      s_listen = -1;
static int      s_client = -1;
static int32_t  s_rcvto_ms = -1;          /* SO_RCVTIMEO currently set on s_client */
static uint32_t s_next_listen_try_ms;
static bool     s_started;

static uint32_t now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static void peer_str(const struct sockaddr_in *a, char *out, size_t n)
{
    ip4addr_ntoa_r((const ip4_addr_t *)&a->sin_addr, out, (int)n);
}

static void drop_client(const char *why)
{
    if (s_client >= 0) {
        log_info(TAG, "client closed (%s)", why);
        (void)lwip_close(s_client);
        s_client = -1;
        s_rcvto_ms = -1;
    }
}

static bool peer_allowed(const struct sockaddr_in *a)
{
#if SPLC_HOST_MODBUS_TCP_ALLOW_NET != 0UL
    const uint32_t ip = lwip_ntohl(a->sin_addr.s_addr);
    return (ip & (uint32_t)SPLC_HOST_MODBUS_TCP_ALLOW_MASK) ==
           ((uint32_t)SPLC_HOST_MODBUS_TCP_ALLOW_NET & (uint32_t)SPLC_HOST_MODBUS_TCP_ALLOW_MASK);
#else
    (void)a;
    return true;
#endif
}

static void open_listener(void)
{
    int fd = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        log_warn(TAG, "socket() failed (errno %d), retrying", errno);
        return;
    }
    int opt = 1;
    (void)lwip_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in sa = {0};
    sa.sin_family      = AF_INET;
    sa.sin_port        = lwip_htons((uint16_t)SPLC_HOST_MODBUS_TCP_PORT);
    sa.sin_addr.s_addr = lwip_htonl(INADDR_ANY);

    if (lwip_bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0 ||
        lwip_listen(fd, LISTEN_BACKLOG) < 0 ||
        lwip_fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
        log_warn(TAG, "bind/listen on port %d failed (errno %d), retrying",
                 (int)SPLC_HOST_MODBUS_TCP_PORT, errno);
        (void)lwip_close(fd);
        return;
    }
    s_listen = fd;
    log_info(TAG, "listening on port %d", (int)SPLC_HOST_MODBUS_TCP_PORT);
}

static void accept_one(void)
{
    struct sockaddr_in peer;
    socklen_t plen = sizeof(peer);
    int fd = lwip_accept(s_listen, (struct sockaddr *)&peer, &plen);
    if (fd < 0) {
        return;                       /* EWOULDBLOCK: nobody is connecting */
    }

    char ip[16];
    peer_str(&peer, ip, sizeof(ip));

    if (!peer_allowed(&peer)) {
        log_warn(TAG, "peer %s rejected (not in allowed network)", ip);
        (void)lwip_close(fd);
        return;
    }
    /* One App at a time: the newest connection wins. */
    drop_client("replaced by a new connection");

    int opt = 1;
    (void)lwip_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));   /* ICD: TCP_NODELAY */
#if defined(LWIP_TCP_KEEPALIVE) && LWIP_TCP_KEEPALIVE
    (void)lwip_setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt));
#endif
    s_client = fd;
    s_rcvto_ms = -1;
    log_info(TAG, "client %s connected", ip);
}

/* transport->process(): once per scan, and while SimplePLC waits inside read(). */
static void tcp_process(void *ctx)
{
    (void)ctx;
    if (s_listen < 0) {
        const uint32_t t = now_ms();
        if ((int32_t)(t - s_next_listen_try_ms) >= 0) {
            s_next_listen_try_ms = t + LISTEN_RETRY_MS;
            if (network_get_active_interface() != NET_IF_NONE) {
                open_listener();
            }
        }
        return;
    }
    accept_one();
}

/* nanoMODBUS read contract: n > 0 bytes, 0 = nothing (timeout / no client),
 * < 0 = hard error. A closed or broken socket is dropped here and reported as
 * "nothing": the next accept() brings the next App. */
static int32_t tcp_read(uint8_t *buf, uint16_t count, int32_t timeout_ms, void *ctx)
{
    (void)ctx;
    if (s_client < 0 || count == 0U) {
        return 0;
    }

    int flags = 0;
    if (timeout_ms <= 0) {
        flags = MSG_DONTWAIT;         /* polling for the start of a request */
    } else if (timeout_ms != s_rcvto_ms) {
        struct timeval tv = { .tv_sec = timeout_ms / 1000,
                              .tv_usec = (timeout_ms % 1000) * 1000 };
        (void)lwip_setsockopt(s_client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        s_rcvto_ms = timeout_ms;
    }

    int n = lwip_recv(s_client, buf, count, flags);
    if (n > 0) {
        return n;
    }
    if (n == 0) {
        drop_client("closed by peer");          /* orderly FIN */
        return 0;
    }
    if (errno == EWOULDBLOCK || errno == EAGAIN) {
        return 0;                               /* nothing yet / inter-byte timeout */
    }
    drop_client("receive error");
    return 0;
}

static int32_t tcp_write(const uint8_t *buf, uint16_t count, int32_t timeout_ms, void *ctx)
{
    (void)ctx;
    (void)timeout_ms;                 /* nanoMODBUS passes the 5 ms byte timeout: too short here */
    if (s_client < 0) {
        return -1;
    }

    uint16_t sent = 0U;
    const uint32_t start = now_ms();
    while (sent < count) {
        int n = lwip_send(s_client, buf + sent, (size_t)(count - sent), MSG_DONTWAIT);
        if (n > 0) {
            sent = (uint16_t)(sent + (uint16_t)n);
            continue;
        }
        if (n < 0 && (errno == EWOULDBLOCK || errno == EAGAIN) &&
            (now_ms() - start) < SPLC_HOST_MODBUS_TCP_TX_TIMEOUT_MS) {
            vTaskDelay(1);            /* send buffer full: give lwIP a tick */
            continue;
        }
        drop_client("send error");
        return -1;
    }
    return (int32_t)count;
}

bool splc_modbus_tcp_port_start(void)
{
    if (s_started) {
        log_error(TAG, "splc_modbus_tcp_port_start() called twice");
        return false;
    }
    const modbus_transport_t t = {
        .ctx     = NULL,
        .read    = tcp_read,
        .write   = tcp_write,
        .process = tcp_process,
        .kind    = MODBUS_TRANSPORT_KIND_TCP,
        .unit_id = 0,                 /* ignored for TCP */
    };
    if (!plc_modbus_cfg_add_transport(&t)) {
        log_error(TAG, "SimplePLC refused the TCP transport");
        return false;
    }
    s_started = true;
    log_info(TAG, "Modbus TCP transport registered (port %d, lazy listen)",
             (int)SPLC_HOST_MODBUS_TCP_PORT);
    return true;
}

#endif /* SPLC_HOST_MODBUS_TCP_ENABLE */