/* Compile-only contract test: a consumer must need only the public header. */
#include "st67w611.h"
#include <stddef.h>

static void consumer_event(st67w611_event_t event, void *data, void *context)
{
    (void)event;
    (void)data;
    (void)context;
}

static void consumer_scan(st67w611_status_t status,
                          const st67w611_scan_result_t *result,
                          void *context)
{
    (void)status;
    (void)result;
    (void)context;
}

static st67w611_status_t consumer_lifecycle(void *context)
{
    (void)context;
    return ST67W611_OK;
}

int st67w611_api_compile_test(void)
{
    static const st67w611_lifecycle_hooks_t lifecycle = {
        .before_transport_restart = consumer_lifecycle,
        .after_transport_restart = consumer_lifecycle,
        .context = NULL,
    };
    st67w611_config_t config = {
        .event_cb = consumer_event,
        .user_context = 0,
        .lifecycle = &lifecycle,
        .recovery = { 2U, 100U, 3000U }
    };
    st67w611_state_t state;
    st67w611_diagnostics_t diagnostics;
    uint8_t mac[6];
    int8_t rssi;

    (void)st67w611_init(&config);
    (void)st67w611_start();
    (void)st67w611_get_state(&state);
    (void)st67w611_get_mac(mac);
    (void)st67w611_get_rssi(&rssi);
    (void)st67w611_get_diagnostics(&diagnostics);
    (void)st67w611_scan(consumer_scan, NULL);
    (void)st67w611_get_ip(mac, mac, mac);
    (void)st67w611_ping("gateway", 1000U);
    (void)st67w611_connect("ssid", "password");
    (void)st67w611_disconnect();
    (void)st67w611_reset();
    (void)st67w611_stop();
    return 0;
}
