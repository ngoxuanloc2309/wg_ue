/**
 * @file rs485_driver.c
 * @brief RS485 UART Driver - Interrupt/DMA-driven with FreeRTOS StreamBuffer
 */
#include "rs485_driver.h"
#include "logger.h"
#include <string.h>

/* Implemented by the shell module; kept as a narrow hook so this driver can
 * retain the single HAL UART receive callback for RS485 and LPUART. */
extern void shell_uart_rx_complete(UART_HandleTypeDef *huart);

static const char *TAG = "rs485";

/* RS485 Port Handles */
static rs485_handle_t rs485_ports[RS485_MAX_PORTS] = {
    { RS485_PORT0_UART, RS485_PORT0_DE_PORT, RS485_PORT0_DE_PIN, NULL, NULL, NULL, NULL, RS485_OWNER_NONE, RS485_STATE_DISABLED, {0}, {0}, 0 },
    { RS485_PORT1_UART, RS485_PORT1_DE_PORT, RS485_PORT1_DE_PIN, NULL, NULL, NULL, NULL, RS485_OWNER_NONE, RS485_STATE_DISABLED, {0}, {0}, 0 },
    { RS485_PORT2_UART, RS485_PORT2_DE_PORT, RS485_PORT2_DE_PIN, NULL, NULL, NULL, NULL, RS485_OWNER_NONE, RS485_STATE_DISABLED, {0}, {0}, 0 },
};

void rs485_set_de(uint8_t port, uint8_t state) {
    if (port >= RS485_MAX_PORTS) return;
    GPIO_PinState pin_state = (state) ? GPIO_PIN_SET : GPIO_PIN_RESET;
    HAL_GPIO_WritePin(rs485_ports[port].de_port, rs485_ports[port].de_pin, pin_state);
}

int rs485_port_init(void) {
    for (int port = 0; port < RS485_MAX_PORTS; port++) {
        rs485_handle_t *p = &rs485_ports[port];

        /* DE Pin configuration */
        GPIO_InitTypeDef GPIO_InitStruct = {0};
        GPIO_InitStruct.Pin = p->de_pin;
        GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(p->de_port, &GPIO_InitStruct);
        rs485_set_de(port, 0);

        /* FreeRTOS primitives */
        if (p->rx_stream == NULL) {
            p->rx_stream = xStreamBufferCreate(RS485_RX_BUF_SIZE, 1);
        }
        if (p->tx_sem == NULL) {
            p->tx_sem = xSemaphoreCreateBinary();
            xSemaphoreGive(p->tx_sem);
        }
        if (p->mutex == NULL) {
            p->mutex = xSemaphoreCreateMutex();
        }
        if (p->owner_mutex == NULL) {
            p->owner_mutex = xSemaphoreCreateMutex();
        }

        memset(&p->counters, 0, sizeof(rs485_counters_t));
        p->state = RS485_STATE_STOPPED;
    }
    LOGI(TAG, "RS485 Port Manager initialized");
    return 0;
}

int rs485_port_acquire(uint8_t port, rs485_owner_t owner)
{
    if (port >= RS485_MAX_PORTS || owner == RS485_OWNER_NONE) return -1;
    rs485_handle_t *p = &rs485_ports[port];
    if (p->owner_mutex == NULL) return -1;
    xSemaphoreTake(p->owner_mutex, portMAX_DELAY);
    int ret = (p->owner == RS485_OWNER_NONE || p->owner == owner) ? 0 : -1;
    if (ret == 0) p->owner = owner;
    xSemaphoreGive(p->owner_mutex);
    return ret;
}

int rs485_port_release(uint8_t port, rs485_owner_t owner)
{
    if (port >= RS485_MAX_PORTS || owner == RS485_OWNER_NONE) return -1;
    rs485_handle_t *p = &rs485_ports[port];
    if (p->owner_mutex == NULL) return -1;
    xSemaphoreTake(p->owner_mutex, portMAX_DELAY);
    int ret = p->owner == owner ? 0 : -1;
    if (ret == 0) p->owner = RS485_OWNER_NONE;
    xSemaphoreGive(p->owner_mutex);
    return ret;
}

int rs485_port_wait_tx_idle(uint8_t port, uint32_t timeout_ms) {
    if (port >= RS485_MAX_PORTS) return -1;
    rs485_handle_t *p = &rs485_ports[port];
    
    uint32_t start_tick = HAL_GetTick();
    while (__HAL_UART_GET_FLAG(p->huart, UART_FLAG_TC) == RESET) {
        if (HAL_GetTick() - start_tick > timeout_ms) {
            return -1;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return 0;
}

int rs485_port_reconfigure(uint8_t port, const rs485_runtime_config_t *cfg) {
    if (port >= RS485_MAX_PORTS || cfg == NULL) return -1;
    rs485_handle_t *p = &rs485_ports[port];

    xSemaphoreTake(p->mutex, portMAX_DELAY);
    
    p->state = RS485_STATE_RECONFIGURING;

    /* Wait for idle and flush */
    rs485_port_wait_tx_idle(port, 100);
    rs485_set_de(port, 0);
    HAL_UART_Abort(p->huart);
    if (p->rx_stream) xStreamBufferReset(p->rx_stream);
    
    HAL_UART_DeInit(p->huart);

    /* Apply new config */
    p->current_cfg = *cfg;
    
    p->huart->Init.BaudRate = cfg->baudrate;
    p->huart->Init.WordLength = (cfg->data_bits == 9) ? UART_WORDLENGTH_9B : UART_WORDLENGTH_8B;
    p->huart->Init.StopBits = (cfg->stop_bits == 2) ? UART_STOPBITS_2 : UART_STOPBITS_1;
    switch(cfg->parity) {
        case 1: p->huart->Init.Parity = UART_PARITY_ODD; break;
        case 2: p->huart->Init.Parity = UART_PARITY_EVEN; break;
        default: p->huart->Init.Parity = UART_PARITY_NONE; break;
    }
    p->huart->Init.Mode = UART_MODE_TX_RX;
    p->huart->Init.HwFlowCtl = UART_HWCONTROL_NONE;
    p->huart->Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(p->huart) != HAL_OK) {
        p->state = RS485_STATE_ERROR;
        xSemaphoreGive(p->mutex);
        return -1;
    }

    /* Enable NVIC */
    IRQn_Type irq_type;
    if (p->huart == &huart3) irq_type = USART3_IRQn;
    else if (p->huart == &huart6) irq_type = USART6_IRQn;
    else if (p->huart == &huart10) irq_type = USART10_IRQn;
    else {
        xSemaphoreGive(p->mutex);
        return -1;
    }

    HAL_NVIC_SetPriority(irq_type, 5, 0);
    HAL_NVIC_EnableIRQ(irq_type);

    /* Start RX Interrupt */
    HAL_UART_Receive_IT(p->huart, &p->rx_byte, 1);

    p->state = RS485_STATE_RUNNING;
    xSemaphoreGive(p->mutex);
    
    LOGI(TAG, "Port %d reconfigured: %lu %s", port, cfg->baudrate, cfg->parity ? "Odd/Even" : "None");
    return 0;
}

int rs485_port_get_status(uint8_t port, rs485_port_status_t *out) {
    if (port >= RS485_MAX_PORTS || out == NULL) return -1;
    rs485_handle_t *p = &rs485_ports[port];
    out->state = p->state;
    out->owner = p->owner;
    out->counters = p->counters;
    out->config = p->current_cfg;
    return 0;
}

void rs485_flush(uint8_t port) {
    if (port >= RS485_MAX_PORTS) return;
    rs485_handle_t *p = &rs485_ports[port];
    if (p->rx_stream) xStreamBufferReset(p->rx_stream);
}

int32_t rs485_send(uint8_t port, const uint8_t *data, uint16_t len) {
    if (port >= RS485_MAX_PORTS) return -1;
    rs485_handle_t *p = &rs485_ports[port];
    if (p->owner == RS485_OWNER_NONE || p->state != RS485_STATE_RUNNING) return -1;

    xSemaphoreTake(p->mutex, portMAX_DELAY);
    rs485_set_de(port, 1);
    
    /* Take semaphore to wait for TX complete */
    xSemaphoreTake(p->tx_sem, 0); 
    
    if (HAL_UART_Transmit_IT(p->huart, (uint8_t*)data, len) != HAL_OK) {
        rs485_set_de(port, 0);
        xSemaphoreGive(p->mutex);
        return -1;
    }

    /* Wait for transmission to complete */
    if (xSemaphoreTake(p->tx_sem, pdMS_TO_TICKS(1000)) != pdTRUE) {
        HAL_UART_AbortTransmit(p->huart);
        rs485_set_de(port, 0);
        p->counters.timeouts++;
        xSemaphoreGive(p->mutex);
        return -1;
    }

    /* Wait for TC flag */
    rs485_port_wait_tx_idle(port, 100);
    
    rs485_set_de(port, 0);
    p->counters.tx_bytes += len;
    p->counters.tx_frames++;
    p->counters.last_activity_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    xSemaphoreGive(p->mutex);
    return len;
}

int32_t rs485_recv_polling(uint8_t port, uint8_t *data, uint16_t max_len, uint32_t timeout_ms) {
    if (port >= RS485_MAX_PORTS) return -1;
    rs485_handle_t *p = &rs485_ports[port];
    if (p->owner == RS485_OWNER_NONE || p->state != RS485_STATE_RUNNING) return -1;
    if (!p->rx_stream) return -1;

    size_t received = xStreamBufferReceive(p->rx_stream, data, max_len, pdMS_TO_TICKS(timeout_ms));
    if (received == 0) {
        p->counters.timeouts++;
    } else {
        p->counters.rx_frames++;
    }
    return (int32_t)received;
}

/* Callbacks from HAL */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    shell_uart_rx_complete(huart);
    int i = -1;
    if (huart == RS485_PORT0_UART) i = 0;
    else if (huart == RS485_PORT1_UART) i = 1;
    else if (huart == RS485_PORT2_UART) i = 2;
    
    if (i >= 0) {
        rs485_handle_t *p = &rs485_ports[i];
        p->counters.rx_bytes++;
        if (p->rx_stream) {
            size_t queued = xStreamBufferSendFromISR(p->rx_stream, &p->rx_byte, 1, &xHigherPriorityTaskWoken);
            if (queued == 0) {
                p->counters.overflows++;
            }
        }
        HAL_UART_Receive_IT(huart, &p->rx_byte, 1);
    }
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    int i = -1;
    if (huart == RS485_PORT0_UART) i = 0;
    else if (huart == RS485_PORT1_UART) i = 1;
    else if (huart == RS485_PORT2_UART) i = 2;
    
    if (i >= 0) {
        if (rs485_ports[i].tx_sem) {
            xSemaphoreGiveFromISR(rs485_ports[i].tx_sem, &xHigherPriorityTaskWoken);
        }
    }
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void rs485_uart_error_callback(UART_HandleTypeDef *huart) {
    int i = -1;
    if (huart == RS485_PORT0_UART) i = 0;
    else if (huart == RS485_PORT1_UART) i = 1;
    else if (huart == RS485_PORT2_UART) i = 2;
    
    if (i >= 0) {
        rs485_handle_t *p = &rs485_ports[i];
        p->counters.hw_errors++;
        p->counters.last_error_code = huart->ErrorCode;
        /* Restart RX */
        HAL_UART_Receive_IT(huart, &p->rx_byte, 1);
    }
}

/* IRQ Handlers */
void USART3_IRQHandler(void) { HAL_UART_IRQHandler(&huart3); }
void USART6_IRQHandler(void) { HAL_UART_IRQHandler(&huart6); }
void USART10_IRQHandler(void) { HAL_UART_IRQHandler(&huart10); }

