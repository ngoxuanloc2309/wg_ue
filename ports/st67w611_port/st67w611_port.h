#ifndef ST67W611_CONSUMER_PORT_H
#define ST67W611_CONSUMER_PORT_H

#include <stdint.h>
#include "st67w611.h"

extern const st67w611_port_ops_t st67w611_port_ops;

int st67w611_port_collect_diagnostics(
    void *context,
    st67w611_port_diagnostics_t *diagnostics);

void st67w611_port_log_diagnostics(void);
void st67w611_port_print_pin_states(void);
int32_t st67w611_port_loopback_test(uint32_t *mismatch_count);

#endif
