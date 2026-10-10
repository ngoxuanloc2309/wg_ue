/* Compile-only mock contract. The real host test runner can replace these
 * callbacks with fault injection without including vendor or HAL headers. */
#include "st67w611.h"
#include <stddef.h>

typedef struct {
    int transfer_result;
    int dma_result;
    int reset_result;
    unsigned completion_count;
} st67w611_mock_port_t;

static int mock_init(void *context,
                     st67w611_transfer_complete_cb_t complete_cb,
                     void *complete_context)
{
    (void)context;
    (void)complete_cb;
    (void)complete_context;
    return 0;
}

static int mock_transfer(void *context, const void *tx, void *rx,
                         uint16_t length, uint32_t timeout_ms)
{
    st67w611_mock_port_t *mock = (st67w611_mock_port_t *)context;
    (void)tx;
    (void)rx;
    (void)length;
    (void)timeout_ms;
    return mock->transfer_result;
}

static int mock_dma(void *context, const void *tx, void *rx, uint16_t length)
{
    st67w611_mock_port_t *mock = (st67w611_mock_port_t *)context;
    (void)tx;
    (void)rx;
    (void)length;
    return mock->dma_result;
}

static int mock_reset(void *context, uint32_t timeout_ms)
{
    st67w611_mock_port_t *mock = (st67w611_mock_port_t *)context;
    (void)timeout_ms;
    return mock->reset_result;
}

int st67w611_mock_port_contract_test(void)
{
    st67w611_mock_port_t mock = {0};
    const st67w611_port_ops_t ops = {
        .init = mock_init,
        .transfer = mock_transfer,
        .transfer_dma = mock_dma,
        .hardware_reset = mock_reset
    };
    return ops.transfer(&mock, NULL, NULL, 0U, 0U) == 0 ? 0 : 1;
}
