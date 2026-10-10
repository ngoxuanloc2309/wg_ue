/**
 * @file gateway.h
 * @brief Gateway State Machine Header
 */
#ifndef __GATEWAY_H__
#define __GATEWAY_H__

#include <stdint.h>

typedef enum {
    GW_STATE_INIT,
    GW_STATE_NET_CONNECTING,
    GW_STATE_NET_OK,
    GW_STATE_MODBUS_OK,
    GW_STATE_TELEMETRY_OK,
    GW_STATE_RUNNING,
    GW_STATE_ERROR
} gateway_state_t;

gateway_state_t gateway_get_state(void);
void gateway_set_state(gateway_state_t state);
const char* gateway_state_str(gateway_state_t state);

void gateway_app_start(void);

#endif /* __GATEWAY_H__ */
