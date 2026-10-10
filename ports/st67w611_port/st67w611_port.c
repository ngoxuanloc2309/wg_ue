#include "st67w611_port.h"
#include "st67w611_port_events.h"
#include "logger.h"
#if __has_include("st67w611_conf.h")
#include "st67w611_conf.h"
#else
#error "[ST67W611] st67w611_conf.h is required"
#endif
#include <string.h>
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"

#define SPI_MAX_XFER_SIZE 2048U

static const char *TAG = "st67_port";
static uint8_t dummy_buffer[SPI_MAX_XFER_SIZE];
static st67w611_transfer_complete_cb_t complete_cb;
static void *complete_context;
static uint8_t initialized;
static volatile struct {
    uint32_t rdy_rising;
    uint32_t rdy_falling;
    uint32_t cs_assert;
    uint32_t transfer_count;
    uint32_t timeout_count;
    uint32_t error_count;
    uint32_t dma_error_count;
} port_diag;
static volatile uint8_t dma_active;
static volatile uint8_t dma_cancelled;


static int port_init(void *context,
                     st67w611_transfer_complete_cb_t callback,
                     void *callback_context)
{
    (void)context;
    complete_cb = callback;
    complete_context = callback_context;
    dma_active = 0U;
    dma_cancelled = 0U;

    if (ST67_SPI_HANDLE->Instance == NULL ||
        ST67_SPI_HANDLE->State == HAL_SPI_STATE_RESET) {
        return -1;
    }

    HAL_GPIO_WritePin(ST67_NCS_GPIO_Port, ST67_NCS_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(ST67_BOOT_GPIO_Port, ST67_BOOT_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(ST67_WAKEUP_GPIO_Port, ST67_WAKEUP_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(ST67_EN_GPIO_Port, ST67_EN_Pin, GPIO_PIN_RESET);
    vTaskDelay(pdMS_TO_TICKS(50U));
    HAL_GPIO_WritePin(ST67_EN_GPIO_Port, ST67_EN_Pin, GPIO_PIN_SET);
    vTaskDelay(pdMS_TO_TICKS(100U));
    initialized = 1U;
    LOGI(TAG, "ST67 port initialized");
    return 0;
}

static int port_deinit(void *context)
{
    (void)context;
    (void)HAL_SPI_DMAStop(ST67_SPI_HANDLE);
    dma_active = 0U;
    complete_cb = NULL;
    complete_context = NULL;
    HAL_GPIO_WritePin(ST67_EN_GPIO_Port, ST67_EN_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(ST67_BOOT_GPIO_Port, ST67_BOOT_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(ST67_WAKEUP_GPIO_Port, ST67_WAKEUP_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(ST67_NCS_GPIO_Port, ST67_NCS_Pin, GPIO_PIN_RESET);
    initialized = 0U;
    return 0;
}

static int port_transfer(void *context, const void *tx, void *rx,
                         uint16_t length, uint32_t timeout_ms)
{
    HAL_StatusTypeDef status;
    (void)context;
    if (!initialized || length > SPI_MAX_XFER_SIZE) return -1;
    if (length == 0U) return 0;

#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    if (tx != NULL) SCB_CleanDCache_by_Addr((uint32_t *)tx, length);
#endif
    if (tx != NULL) {
        status = HAL_SPI_TransmitReceive(ST67_SPI_HANDLE,
            (uint8_t *)tx, (uint8_t *)rx, length, timeout_ms);
    } else {
        memset(dummy_buffer, 0, length);
        status = HAL_SPI_TransmitReceive(ST67_SPI_HANDLE,
            dummy_buffer, (uint8_t *)rx, length, timeout_ms);
    }
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    if (rx != NULL) SCB_InvalidateDCache_by_Addr((uint32_t *)rx, length);
#endif
    port_diag.transfer_count++;
    if (status == HAL_TIMEOUT) port_diag.timeout_count++;
    if (status != HAL_OK) {
        port_diag.error_count++;
        return -1;
    }
    return 0;
}

static int port_transfer_dma(void *context, const void *tx, void *rx,
                             uint16_t length)
{
    HAL_StatusTypeDef status;
    (void)context;
    if (!initialized || dma_active || length > SPI_MAX_XFER_SIZE) return -1;
    if (length == 0U) return 0;
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    if (tx != NULL) SCB_CleanDCache_by_Addr((uint32_t *)tx, length);
    if (rx != NULL) SCB_InvalidateDCache_by_Addr((uint32_t *)rx, length);
#endif
    dma_cancelled = 0U;
    dma_active = 1U;
    if (tx != NULL) {
        status = HAL_SPI_TransmitReceive_DMA(ST67_SPI_HANDLE,
            (uint8_t *)tx, (uint8_t *)rx, length);
    } else {
        memset(dummy_buffer, 0, length);
        status = HAL_SPI_TransmitReceive_DMA(ST67_SPI_HANDLE,
            dummy_buffer, (uint8_t *)rx, length);
    }
    port_diag.transfer_count++;
    if (status != HAL_OK) {
        dma_active = 0U;
        port_diag.dma_error_count++;
        return -1;
    }
    return 0;
}

static int port_cancel_transfer(void *context)
{
    (void)context;
    dma_cancelled = 1U;
    dma_active = 0U;
    return HAL_SPI_DMAStop(ST67_SPI_HANDLE) == HAL_OK ? 0 : -1;
}

static int port_set_cs(void *context, bool asserted)
{
    (void)context;
    HAL_GPIO_WritePin(ST67_NCS_GPIO_Port, ST67_NCS_Pin,
                      asserted ? GPIO_PIN_SET : GPIO_PIN_RESET);
    if (asserted) port_diag.cs_assert++;
    return 0;
}

static bool port_read_ready(void *context)
{
    (void)context;
    return HAL_GPIO_ReadPin(ST67_RDY_GPIO_Port, ST67_RDY_Pin) == GPIO_PIN_SET;
}

static int port_hardware_reset(void *context, uint32_t timeout_ms)
{
    (void)context;
    (void)timeout_ms;
    HAL_GPIO_WritePin(ST67_EN_GPIO_Port, ST67_EN_Pin, GPIO_PIN_RESET);
    vTaskDelay(pdMS_TO_TICKS(50U));
    HAL_GPIO_WritePin(ST67_EN_GPIO_Port, ST67_EN_Pin, GPIO_PIN_SET);
    vTaskDelay(pdMS_TO_TICKS(100U));
    return 0;
}

static void port_delay_ms(void *context, uint32_t delay_ms)
{
    (void)context;
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
}

const st67w611_port_ops_t st67w611_port_ops = {
    .init = port_init,
    .deinit = port_deinit,
    .transfer = port_transfer,
    .transfer_dma = port_transfer_dma,
    .cancel_transfer = port_cancel_transfer,
    .set_cs = port_set_cs,
    .set_enable = NULL,
    .set_boot = NULL,
    .set_wakeup = NULL,
    .read_ready = port_read_ready,
    .hardware_reset = port_hardware_reset,
    .delay_ms = port_delay_ms,
    .get_diagnostics = st67w611_port_collect_diagnostics
};

int st67w611_port_collect_diagnostics(void *context,
                                      st67w611_port_diagnostics_t *diagnostics)
{
    (void)context;
    if (diagnostics == NULL) return -1;
    diagnostics->transfer_count = port_diag.transfer_count;
    diagnostics->timeout_count = port_diag.timeout_count;
    diagnostics->error_count = port_diag.error_count;
    diagnostics->dma_error_count = port_diag.dma_error_count;
    diagnostics->ready_rising_count = port_diag.rdy_rising;
    diagnostics->ready_falling_count = port_diag.rdy_falling;
    return 0;
}

void HAL_GPIO_EXTI_Rising_Callback(uint16_t pin)
{
    if (pin == ST67_RDY_Pin) {
        port_diag.rdy_rising++;
        st67w611_port_notify_ready_rising();
    }
}

void HAL_GPIO_EXTI_Falling_Callback(uint16_t pin)
{
    if (pin == ST67_RDY_Pin) {
        port_diag.rdy_falling++;
        st67w611_port_notify_ready_falling();
    }
}

static void port_complete(int status)
{
    if (!dma_active || dma_cancelled) return;
    dma_active = 0U;
    if (complete_cb != NULL) complete_cb(complete_context, status);
}

void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi == ST67_SPI_HANDLE) port_complete(0);
}

void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi == ST67_SPI_HANDLE) port_complete(0);
}

void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi == ST67_SPI_HANDLE) port_complete(0);
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi == ST67_SPI_HANDLE) {
        port_diag.dma_error_count++;
        port_complete(-1);
    }
}

void st67w611_port_log_diagnostics(void)
{
    LOGI(TAG, "ST67 port: rdy rise=%lu fall=%lu cs=%lu xfer=%lu timeout=%lu error=%lu dma=%lu",
         (unsigned long)port_diag.rdy_rising,
         (unsigned long)port_diag.rdy_falling,
         (unsigned long)port_diag.cs_assert,
         (unsigned long)port_diag.transfer_count,
         (unsigned long)port_diag.timeout_count,
         (unsigned long)port_diag.error_count,
         (unsigned long)port_diag.dma_error_count);
}

void st67w611_port_print_pin_states(void)
{
    printf("[ST67-PINS] CS=%d RDY=%d EN=%d BOOT=%d WAKE=%d\r\n",
        HAL_GPIO_ReadPin(ST67_NCS_GPIO_Port, ST67_NCS_Pin) == GPIO_PIN_SET,
        HAL_GPIO_ReadPin(ST67_RDY_GPIO_Port, ST67_RDY_Pin) == GPIO_PIN_SET,
        HAL_GPIO_ReadPin(ST67_EN_GPIO_Port, ST67_EN_Pin) == GPIO_PIN_SET,
        HAL_GPIO_ReadPin(ST67_BOOT_GPIO_Port, ST67_BOOT_Pin) == GPIO_PIN_SET,
        HAL_GPIO_ReadPin(ST67_WAKEUP_GPIO_Port, ST67_WAKEUP_Pin) == GPIO_PIN_SET);
}

int32_t st67w611_port_loopback_test(uint32_t *mismatch_count)
{
#if defined(ST67_SPI_LOOPBACK) && ST67_SPI_LOOPBACK
    static const uint8_t pattern[] = {0x00U, 0xFFU, 0xA5U, 0x5AU,
                                      0x3CU, 0xC3U, 0x96U, 0x69U};
    uint8_t received[sizeof(pattern)] = {0};
    uint32_t mismatches = 0U;
    if (HAL_SPI_TransmitReceive(ST67_SPI_HANDLE, (uint8_t *)pattern,
                                received, sizeof(pattern), 100U) != HAL_OK) {
        return -1;
    }
    for (size_t i = 0U; i < sizeof(pattern); ++i) {
        if (received[i] != pattern[i]) ++mismatches;
    }
    if (mismatch_count != NULL) *mismatch_count = mismatches;
    return mismatches == 0U ? 0 : 1;
#else
    (void)mismatch_count;
    return -3;
#endif
}
