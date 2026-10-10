/**
 * @file spi_port_port.c
 * @brief SPI porting layer for ST67W611 WiFi module
 *
 * Hardware: STM32H5 SPI1
 * Pins: PB3 (SCK), PB4 (MISO), PB5 (MOSI), PD7 (CS), PD0 (RDY), PD1 (EN)
 *
 * Dependencies:
 * - SPI1 handle (hspi1) must be defined in main.c/CubeMX generated code
 * - DMA handles must be configured in CubeMX
 * - GPIO defines (ST67_xxx_Pin/ST67_xxx_GPIO_Port) from main.h
 */
#include "spi_port.h"
#include "wifi_spi_port.h"
#include "logger.h"
#include "stm32h5xx_hal.h"
#include "main.h"
#include "w61_driver_config.h"
#include <string.h>
#include <stddef.h>
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>

static const char *TAG = "spi_port";

/* Maximum single SPI transfer size (must be >= largest packet: Wi-Fi MTU 1500) */
#define SPI_MAX_XFER_SIZE  2048U

/* Static dummy buffer for RX-only transfers (avoids stack allocation for large transfers) */
static uint8_t spi_dummy_buf[SPI_MAX_XFER_SIZE];

/* SPI handle - must be defined externally (CubeMX generated) */
extern SPI_HandleTypeDef hspi1;

/* DMA handles - defined in gpdma.c or CubeMX generated */
extern DMA_HandleTypeDef handle_GPDMA1_Channel0;
extern DMA_HandleTypeDef handle_GPDMA1_Channel1;

/* SPI transaction complete callback */
static spi_transaction_complete_t spi_complete_cb = NULL;

/* SPI port state */
static uint8_t spi_initialized = 0;
static volatile spi_port_diag_t spi_diag;
static uint32_t spi_trace_budget = 20U;
static volatile uint8_t spi_peer_no_response = 0U;

volatile uint32_t dbg_time_cs_assert = 0;
volatile uint32_t dbg_time_cs_deassert = 0;
volatile uint32_t dbg_time_rdy_rise = 0;
volatile uint32_t dbg_time_rdy_fall = 0;
volatile uint32_t dbg_time_xfer_start = 0;
volatile uint32_t dbg_time_xfer_end = 0;

/* Vendor transaction event hooks. */
extern int32_t spi_on_txn_data_ready(void);
extern int32_t spi_on_header_ack(void);

/**
 * @brief Initialize SPI port for ST67W611
 */
int32_t spi_port_init(spi_transaction_complete_t transaction_complete_cb) {
    if (spi_initialized) {
        LOGW(TAG, "SPI already initialized");
        return 0;
    }

    /* Enable DWT for high-resolution timing */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    /* Validate SPI handle */
    if (hspi1.Instance == NULL) {
        LOGE(TAG, "SPI1 handle not initialized");
        return -1;
    }

    /* Store callback */
    spi_complete_cb = transaction_complete_cb;

    /* Configure GPIO pins for SPI1. SCK/MISO/MOSI and the SPI peripheral are
     * initialized by CubeMX before the scheduler starts. */

    /* ST67W611 uses CS as an active-HIGH signal.
     * Ensure CS is deasserted (LOW) before enabling power to ST67 */
    HAL_GPIO_WritePin(ST67_NCS_GPIO_Port, ST67_NCS_Pin, GPIO_PIN_RESET);
    
    /* Ensure BOOT0 is LOW for Flash boot, and assert WAKEUP */
    HAL_GPIO_WritePin(ST67_BOOT_GPIO_Port, ST67_BOOT_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(ST67_WAKEUP_GPIO_Port, ST67_WAKEUP_Pin, GPIO_PIN_SET);

    /* Hard reset ST67: toggle EN LOW then HIGH */
    HAL_GPIO_WritePin(ST67_EN_GPIO_Port, ST67_EN_Pin, GPIO_PIN_RESET);
    vTaskDelay(pdMS_TO_TICKS(50));
    HAL_GPIO_WritePin(ST67_EN_GPIO_Port, ST67_EN_Pin, GPIO_PIN_SET);
    vTaskDelay(pdMS_TO_TICKS(100));

    /* Do not call HAL_SPI_Init() here. W6X_Init() invokes this port function
     * after MX_SPI1_Init() has already configured SPI1. Reinitializing the HAL
     * handle at this point can reset the DMA/interrupt state used by the
     * vendor SPI transaction engine. */
    if (hspi1.State == HAL_SPI_STATE_RESET) {
        LOGE(TAG, "SPI1 is not initialized by CubeMX");
        return -1;
    }

    spi_initialized = 1;
    LOGI(TAG, "SPI port initialized");

    /* RDY is owned by the transaction engine; this is diagnostic only. */
    GPIO_PinState rdy_state = HAL_GPIO_ReadPin(ST67_RDY_GPIO_Port, ST67_RDY_Pin);
    LOGI(TAG, "ST67W611 RDY after boot: %s (non-blocking check)",
         rdy_state == GPIO_PIN_SET ? "HIGH" : "LOW");
    LOGI(TAG, "BOOT pin: %s, EN pin: %s",
         HAL_GPIO_ReadPin(ST67_BOOT_GPIO_Port, ST67_BOOT_Pin) == GPIO_PIN_SET ? "HIGH" : "LOW",
         HAL_GPIO_ReadPin(ST67_EN_GPIO_Port, ST67_EN_Pin) == GPIO_PIN_SET ? "HIGH" : "LOW");

    return 0;
}

/**
 * @brief De-initialize SPI port
 */
int32_t spi_port_deinit(void) {
    HAL_StatusTypeDef hal_status;

    if (!spi_initialized) {
        return 0;
    }

    /* De-initialize SPI */
    hal_status = HAL_SPI_DeInit(&hspi1);
    if (hal_status != HAL_OK) {
        LOGE(TAG, "HAL_SPI_DeInit failed: %d", hal_status);
    }

    /* Disable module power */
    HAL_GPIO_WritePin(ST67_EN_GPIO_Port, ST67_EN_Pin, GPIO_PIN_RESET);

    /* Reset control pins */
    HAL_GPIO_WritePin(ST67_BOOT_GPIO_Port, ST67_BOOT_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(ST67_WAKEUP_GPIO_Port, ST67_WAKEUP_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(ST67_NCS_GPIO_Port, ST67_NCS_Pin, GPIO_PIN_RESET);

    spi_initialized = 0;
    spi_complete_cb = NULL;

    LOGI(TAG, "SPI port deinitialized");

    return 0;
}

/**
 * @brief Execute SPI transfer in blocking mode (polling)
 */
int32_t spi_port_transfer(void *tx_buf, void *rx_buf, uint16_t len, uint32_t timeout) {
    HAL_StatusTypeDef hal_status;

    spi_diag.transfer_count++;
#if ST67_SPI_DIAG_STATE_TRACE
    printf("[ST67-TRACE] spi_port_transfer entry len=%u\r\n", (unsigned)len);
#endif
    if (spi_trace_budget != 0U) {
        LOGI(TAG, "[ST67-TRACE] polling transfer begin len=%u tx=%s rx=%s",
             (unsigned)len, tx_buf != NULL ? "yes" : "no",
             rx_buf != NULL ? "yes" : "no");
        spi_trace_budget--;
    }

    if (!spi_initialized) {
        return -1;
    }

    if (hspi1.Instance == NULL) {
        return -1;
    }

    /* Validate length */
    if (len == 0) {
        return 0;
    }

    if (len > SPI_MAX_XFER_SIZE) {
        LOGE(TAG, "SPI transfer exceeds max size: %u > %u", len, SPI_MAX_XFER_SIZE);
        return -1;
    }

#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    if (tx_buf != NULL) {
        SCB_CleanDCache_by_Addr((uint32_t *)tx_buf, len);
    }
#endif /* __DCACHE_PRESENT */

    /* Disable DMA requests in SPI1 peripheral so polling mode receives bytes in RXDR */
    CLEAR_BIT(hspi1.Instance->CFG1, SPI_CFG1_RXDMAEN | SPI_CFG1_TXDMAEN);

    if (tx_buf != NULL) {
        if (spi_trace_budget != 0U) {
            const uint8_t *tx = (const uint8_t *)tx_buf;
            int rdy_before = (HAL_GPIO_ReadPin(ST67_RDY_GPIO_Port, ST67_RDY_Pin) == GPIO_PIN_SET) ? 1 : 0;
            LOGI(TAG, "[ST67-TRACE] TX: %02X %02X %02X %02X %02X %02X %02X %02X  RDY_before=%d",
                 tx[0], tx[1], tx[2], tx[3], tx[4], tx[5], tx[6], tx[7], rdy_before);
        }
        /* Full duplex: transmit and receive */
        hal_status = HAL_SPI_TransmitReceive(&hspi1,
                                              (uint8_t *)tx_buf,
                                              (uint8_t *)rx_buf,
                                              len,
                                              timeout);
    } else if (rx_buf != NULL) {
        /* RX only: send zeros on MOSI using static dummy buffer */
        memset(spi_dummy_buf, 0, len);
        hal_status = HAL_SPI_TransmitReceive(&hspi1,
                                              spi_dummy_buf,
                                              (uint8_t *)rx_buf,
                                              len,
                                              timeout);
    } else {
        /* Nothing to do */
        return 0;
    }

#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    if (rx_buf != NULL) {
        SCB_InvalidateDCache_by_Addr((uint32_t *)rx_buf, len);
    }
#endif /* __DCACHE_PRESENT */

    if (hal_status != HAL_OK) {
        if (hal_status == HAL_TIMEOUT) {
            LOGW(TAG, "SPI transfer timeout");
        } else {
            LOGE(TAG, "SPI transfer error: %d", hal_status);
        }
        return -1;
    }

    if (rx_buf != NULL && len == 8U) {
        const uint8_t *rx = (const uint8_t *)rx_buf;
        uint8_t all_zero = 1U;
        for (uint16_t i = 0U; i < len; ++i) {
            if (rx[i] != 0U) {
                all_zero = 0U;
                break;
            }
        }
        if (all_zero) {
            spi_peer_no_response = 1U;
            LOGW(TAG, "ST67 returned no SPI data; stopping retry spin");
            return -1;
        }
    }

    if (spi_trace_budget != 0U) {
        const uint8_t *rx = (const uint8_t *)rx_buf;
        int rdy_after = (HAL_GPIO_ReadPin(ST67_RDY_GPIO_Port, ST67_RDY_Pin) == GPIO_PIN_SET) ? 1 : 0;
        LOGI(TAG, "[ST67-TRACE] RX: %02X %02X %02X %02X %02X %02X %02X %02X  RDY_after=%d",
             rx[0], rx[1], rx[2], rx[3], rx[4], rx[5], rx[6], rx[7], rdy_after);
        spi_trace_budget--;
    }

    return 0;
}

/**
 * @brief Execute SPI transfer using DMA
 */
int32_t spi_port_transfer_dma(void *tx_buf, void *rx_buf, uint16_t len) {
    HAL_StatusTypeDef hal_status;

#if ST67_SPI_DIAG_POLLING
    int32_t polling_status = spi_port_transfer(tx_buf, rx_buf, len, 1000U);
    if ((polling_status == 0) && (spi_complete_cb != NULL)) {
        spi_complete_cb();
    }
    return polling_status;
#endif

    if (!spi_initialized) {
        spi_diag.dma_error_count++;
        return -1;
    }

    if (hspi1.Instance == NULL) {
        return -1;
    }

    /* Validate length */
    if (len == 0) {
        return 0;
    }

    if (len > SPI_MAX_XFER_SIZE) {
        spi_diag.dma_error_count++;
        LOGE(TAG, "SPI DMA transfer exceeds max size: %u > %u", len, SPI_MAX_XFER_SIZE);
        return -1;
    }

    /* Check if DMA is configured */
    if (hspi1.hdmatx == NULL || hspi1.hdmarx == NULL) {
        LOGW(TAG, "DMA not configured, using blocking mode");
        return spi_port_transfer(tx_buf, rx_buf, len, 1000);
    }

    spi_diag.transfer_count++;
    if (spi_trace_budget != 0U) {
        LOGI(TAG, "[ST67-TRACE] DMA transfer begin len=%u tx=%s rx=%s",
             (unsigned)len, tx_buf != NULL ? "yes" : "no",
             rx_buf != NULL ? "yes" : "no");
        spi_trace_budget--;
    }

#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    /* D-Cache maintenance: clean TX before DMA, invalidate RX after DMA */
    if (rx_buf != NULL) {
        SCB_InvalidateDCache_by_Addr((uint32_t *)rx_buf, len);
    }
    if (tx_buf != NULL) {
        SCB_CleanDCache_by_Addr((uint32_t *)tx_buf, len);
    }
#endif /* __DCACHE_PRESENT */

    if (tx_buf != NULL) {
        /* TX + RX */
        hal_status = HAL_SPI_TransmitReceive_DMA(&hspi1,
                                                   (uint8_t *)tx_buf,
                                                   (uint8_t *)rx_buf,
                                                   len);
    } else if (rx_buf != NULL) {
        /* RX only - send 0xFF on MOSI using static dummy buffer */
        memset(spi_dummy_buf, 0, len);
        hal_status = HAL_SPI_TransmitReceive_DMA(&hspi1,
                                                 spi_dummy_buf,
                                                 (uint8_t *)rx_buf,
                                                 len);
    } else {
        return 0;
    }

    if (hal_status != HAL_OK) {
        spi_diag.dma_error_count++;
        LOGE(TAG, "SPI DMA transfer error: %d", hal_status);
        return -1;
    }

    if (spi_trace_budget != 0U) {
        LOGI(TAG, "[ST67-TRACE] DMA transfer accepted");
        spi_trace_budget--;
    }

    return 0;
}

/**
 * @brief Check if ST67W611 module is ready
 * @return 1 if ready, 0 if not ready
 */
int32_t spi_port_is_ready(void) {
    GPIO_PinState pin_state = HAL_GPIO_ReadPin(ST67_RDY_GPIO_Port, ST67_RDY_Pin);

    if (spi_trace_budget != 0U) {
        LOGI(TAG, "[ST67-TRACE] spi_port_is_ready=%s",
             pin_state == GPIO_PIN_SET ? "HIGH" : "LOW");
        spi_trace_budget--;
    }

    return (pin_state == GPIO_PIN_SET) ? 1 : 0;
}

uint8_t spi_port_has_peer_failed(void) {
    return spi_peer_no_response;
}

/**
 * @brief Set SPI chip select state
 * @param state 1 = Assert (CS high), 0 = De-assert (CS low)
 * @return 0 on success, -1 on failure
 *
 * GatewayV2 PCB connects PD7 (ST67_NCS) to ST67W611 CS pin.
 * ST67W611 uses CS as an active-low wake/transaction signal.
 */
int32_t spi_port_set_cs(int32_t state) {
    if (!spi_initialized) {
        return -1;
    }

    if (state == 1) {
        dbg_time_cs_assert = DWT->CYCCNT;
    } else {
        dbg_time_cs_deassert = DWT->CYCCNT;
    }

    /* ST67W611M1 uses CS as an active-HIGH signal */
    HAL_GPIO_WritePin(ST67_NCS_GPIO_Port, ST67_NCS_Pin, 
                      state ? GPIO_PIN_SET : GPIO_PIN_RESET);

    /* Provide CS setup time (~20us) for ST67 SPI slave hardware to wake up */
    for (volatile int delay = 0; delay < 2000; delay++) {
        __NOP();
    }

    if (spi_trace_budget != 0U) {
        LOGI(TAG, "[ST67-TRACE] CS %s",
             state ? "ASSERT" : "DEASSERT");
        spi_trace_budget--;
    }
    if (state) {
        spi_diag.cs_assert++;
    }
    return 0;
}

/**
 * @brief Memory copy function for SPI port
 */
void *spi_port_memcpy(void *dest, const void *src, unsigned int len) {
    return memcpy(dest, src, len);
}

/**
 * @brief EXTI line detection callback.
 * @param GPIO_Pin Specifies the pins connected EXTI line
 */
void HAL_GPIO_EXTI_Rising_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == ST67_RDY_Pin) {
        dbg_time_rdy_rise = DWT->CYCCNT;
        spi_diag.rdy_rising++;
        /* ISR-safe: do not call LOGI here; the logger uses RTOS objects. */
        spi_on_txn_data_ready();
    }
}

void HAL_GPIO_EXTI_Falling_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == ST67_RDY_Pin) {
        dbg_time_rdy_fall = DWT->CYCCNT;
        spi_diag.rdy_falling++;
        /* ISR-safe: do not call LOGI here; the logger uses RTOS objects. */
        spi_on_header_ack();
    }
}

void spi_port_get_diagnostics(spi_port_diag_t *diag)
{
    if (diag != NULL) {
        *diag = spi_diag;
    }
}

void spi_port_log_diagnostics(void)
{
    spi_port_diag_t diag;
    spi_port_get_diagnostics(&diag);
    LOGI(TAG, "[ST67-DIAG] RDY rise=%lu fall=%lu CS=%lu xfer=%lu DMAerr=%lu",
         (unsigned long)diag.rdy_rising,
         (unsigned long)diag.rdy_falling,
         (unsigned long)diag.cs_assert,
         (unsigned long)diag.transfer_count,
         (unsigned long)diag.dma_error_count);
}

int32_t spi_port_loopback_test(uint32_t *mismatch_count)
{
#if defined(ST67_SPI_LOOPBACK) && ST67_SPI_LOOPBACK
    static const uint8_t pattern[] = {0x00U, 0xFFU, 0xA5U, 0x5AU,
                                      0x3CU, 0xC3U, 0x96U, 0x69U};
    uint8_t received[sizeof(pattern)] = {0};
    uint32_t mismatches = 0U;

    if (HAL_GPIO_ReadPin(ST67_EN_GPIO_Port, ST67_EN_Pin) != GPIO_PIN_RESET) {
        return -2;
    }

    HAL_GPIO_WritePin(ST67_NCS_GPIO_Port, ST67_NCS_Pin, GPIO_PIN_RESET);
    if (HAL_SPI_TransmitReceive(&hspi1, (uint8_t *)pattern, received,
                                sizeof(pattern), 100U) != HAL_OK) {
        return -1;
    }

    for (size_t i = 0U; i < sizeof(pattern); ++i) {
        if (received[i] != pattern[i]) {
            mismatches++;
        }
    }
    if (mismatch_count != NULL) {
        *mismatch_count = mismatches;
    }
    return mismatches == 0U ? 0 : 1;
#else
    (void)mismatch_count;
    return -3;
#endif
}

/**
 * @brief SPI DMA TX complete callback
 */
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi) {
    if (hspi == &hspi1 && spi_complete_cb != NULL) {
        spi_complete_cb();
    }
}

/**
 * @brief SPI DMA RX complete callback
 */
void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi) {
    if (hspi == &hspi1 && spi_complete_cb != NULL) {
        spi_complete_cb();
    }
}

/**
 * @brief SPI DMA TX/RX complete callback
 */
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi) {
    if (hspi == &hspi1 && spi_complete_cb != NULL) {
        spi_complete_cb();
    }
}

/**
 * @brief SPI error callback
 */
void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi) {
    if (hspi == &hspi1) {
        spi_diag.dma_error_count++;
    }
}

void spi_port_print_pin_states(void) {
    printf("[ST67-PINS] CS(PD7)=%d SCK(PB3)=%d MOSI(PB5)=%d MISO(PB4)=%d RDY(PD0)=%d EN(PD1)=%d BOOT(PD2)=%d WAKE(PD3)=%d\r\n",
           HAL_GPIO_ReadPin(ST67_NCS_GPIO_Port, ST67_NCS_Pin) == GPIO_PIN_SET ? 1 : 0,
           HAL_GPIO_ReadPin(ST67_SCK_GPIO_Port, ST67_SCK_Pin) == GPIO_PIN_SET ? 1 : 0,
           HAL_GPIO_ReadPin(ST67_MOSI_GPIO_Port, ST67_MOSI_Pin) == GPIO_PIN_SET ? 1 : 0,
           HAL_GPIO_ReadPin(ST67_MISO_GPIO_Port, ST67_MISO_Pin) == GPIO_PIN_SET ? 1 : 0,
           HAL_GPIO_ReadPin(ST67_RDY_GPIO_Port, ST67_RDY_Pin) == GPIO_PIN_SET ? 1 : 0,
           HAL_GPIO_ReadPin(ST67_EN_GPIO_Port, ST67_EN_Pin) == GPIO_PIN_SET ? 1 : 0,
           HAL_GPIO_ReadPin(ST67_BOOT_GPIO_Port, ST67_BOOT_Pin) == GPIO_PIN_SET ? 1 : 0,
           HAL_GPIO_ReadPin(ST67_WAKEUP_GPIO_Port, ST67_WAKEUP_Pin) == GPIO_PIN_SET ? 1 : 0);
}
