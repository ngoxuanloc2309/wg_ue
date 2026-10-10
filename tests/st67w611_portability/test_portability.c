/**
 * @file test_portability.c
 * @brief Portability test for ST67W611 Wi-Fi Driver.
 * 
 * Instructions:
 * Step A (Negative Test): Compile this file WITHOUT st67w611_conf.h in the include path.
 *   - Expected result: Compilation FAILS with '#error "[ST67W611] Fatal error: st67w611_conf.h not found..."'.
 * 
 * Step B (Positive Test): Compile this file WITH a mock st67w611_conf.h in the include path.
 *   - Expected result: Compilation SUCCEEDS without needing main.h or GatewayV2 specific files.
 */

/* The reusable consumer contract must not require vendor or board symbols. */
#include "st67w611.h"

void portability_dummy_func(void) {
    st67w611_state_t state = ST67W611_STATE_OFF;
    (void)state;
}
