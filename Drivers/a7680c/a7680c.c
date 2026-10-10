#include "a7680c.h"
#include "usart.h"
#include "cmsis_os2.h"
#include "logger.h"
#include "app_config.h"
#include "main.h"
#include <string.h>
#include <stdio.h>

#define A7680C_BOOT_TIME_MS 5000

static const char *TAG = "a7680c";

/* UART RX Ring Buffer */
#define LTE_RX_BUF_SIZE 2048
static uint8_t rx_buffer[LTE_RX_BUF_SIZE];
static volatile uint16_t rx_head = 0;
static volatile uint16_t rx_tail = 0;
static volatile uint32_t rx_drop_count = 0;

/* UART RX DMA Buffer */
#define LTE_DMA_BUF_SIZE 512
static uint8_t dma_buffer[LTE_DMA_BUF_SIZE];

static HAL_StatusTypeDef a7680c_start_rx_dma(void) {
    HAL_StatusTypeDef status;

    /* Clear stale status before arming DMA.  In DMA mode STM32 HAL treats
     * FE/NE/ORE as fatal and aborts the whole receive operation.  The modem
     * stream is otherwise valid (and the framing flag can be transient), so
     * leave DMA running and let the idle callback deliver the bytes. */
    __HAL_UART_CLEAR_FLAG(&huart1, UART_CLEAR_FEF | UART_CLEAR_NEF |
                          UART_CLEAR_OREF | UART_CLEAR_IDLEF);
    status = HAL_UARTEx_ReceiveToIdle_DMA(&huart1, dma_buffer, LTE_DMA_BUF_SIZE);
    if (status == HAL_OK) {
        CLEAR_BIT(huart1.Instance->CR3, USART_CR3_EIE);
    }
    return status;
}

static void clear_rx_buffer(void) {
    rx_head = 0;
    rx_tail = 0;
}

static void push_rx_byte(uint8_t byte) {
    uint16_t next = (uint16_t)((rx_head + 1U) % LTE_RX_BUF_SIZE);
    if (next == rx_tail) {
        /* Keep the oldest complete response; never silently corrupt it. */
        rx_drop_count++;
        return;
    }
    rx_buffer[rx_head] = byte;
    rx_head = next;
}

void a7680c_init(void) {
    /* Start RX DMA with Idle Detection */
    (void)a7680c_start_rx_dma();
}

int a7680c_receive(uint8_t *buf, uint16_t max_len) {
    int count = 0;
    while (rx_tail != rx_head && count < max_len) {
        buf[count++] = rx_buffer[rx_tail];
        rx_tail = (rx_tail + 1) % LTE_RX_BUF_SIZE;
    }
    return count;
}

int a7680c_transmit(const uint8_t *data, uint16_t len) {
    if (HAL_UART_Transmit(&huart1, (uint8_t*)data, len, 500) == HAL_OK) {
        return len;
    }
    return -1;
}

/* --- UART RX DMA Idle Interrupt Hook --- */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size) {
    if (huart->Instance == USART1) {
        /* Copy received chunk into Ring Buffer */
        if (Size > LTE_DMA_BUF_SIZE) {
            Size = LTE_DMA_BUF_SIZE;
        }
        for (uint16_t i = 0; i < Size; i++) {
            push_rx_byte(dma_buffer[i]);
        }
        /* Restart DMA to wait for next chunk */
        (void)a7680c_start_rx_dma();
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART1) {
        HAL_StatusTypeDef status;

        LOGE(TAG, "UART error 0x%08lx; aborting RX DMA", (unsigned long)huart->ErrorCode);
        /* On an FE/NE/ORE, HAL may leave the DMA state BUSY.  Starting a new
         * ReceiveToIdle operation directly can therefore silently fail. */
        (void)HAL_UART_AbortReceive(&huart1);
        status = a7680c_start_rx_dma();
        if (status != HAL_OK) {
            LOGE(TAG, "RX DMA restart failed: %d", (int)status);
        }
    } else {
        extern void rs485_uart_error_callback(UART_HandleTypeDef *huart);
        rs485_uart_error_callback(huart);
    }
}

static int send_at_command_capture(const char *cmd, const char *expected_resp,
                                   uint32_t timeout_ms, char *response,
                                   size_t response_size) {
    clear_rx_buffer();
    LOGD(TAG, "AT> %s", cmd);
    if (HAL_UART_Transmit(&huart1, (uint8_t*)cmd, strlen(cmd), 100) != HAL_OK) {
        LOGE(TAG, "AT TX failed");
        return -1;
    }
    
    uint32_t start = osKernelGetTickCount();
    char resp_buf[512] = {0};
    int resp_idx = 0;
    
    while ((osKernelGetTickCount() - start) < timeout_ms) {
        uint8_t tmp[32];
        int n = a7680c_receive(tmp, sizeof(tmp));
        for (int i = 0; i < n; i++) {
            if (resp_idx < sizeof(resp_buf) - 1) {
                resp_buf[resp_idx++] = tmp[i];
                resp_buf[resp_idx] = '\0';
                if (response != NULL && response_size > 0U) {
                    size_t copy_len = (size_t)resp_idx;
                    if (copy_len >= response_size) {
                        copy_len = response_size - 1U;
                    }
                    memcpy(response, resp_buf, copy_len);
                    response[copy_len] = '\0';
                }
                if (strstr(resp_buf, expected_resp) != NULL) {
                    LOGD(TAG, "AT< (OK)");
                    return 0;
                }
                if (strstr(resp_buf, "ERROR") != NULL) {
                    LOGE(TAG, "AT< (ERROR)");
                    return -1;
                }
            }
        }
        osDelay(10);
    }
    LOGE(TAG, "AT Timeout");
    return -1;
}

static int send_at_command(const char *cmd, const char *expected_resp,
                           uint32_t timeout_ms) {
    return send_at_command_capture(cmd, expected_resp, timeout_ms, NULL, 0U);
}

static int query_and_log(const char *cmd, const char *label, uint32_t timeout_ms) {
    char response[192] = {0};
    int status = send_at_command_capture(cmd, "OK", timeout_ms,
                                         response, sizeof(response));
    if (status == 0) {
        LOGI(TAG, "%s: %s", label, response);
    } else {
        LOGW(TAG, "%s query failed: %s", label, response);
    }
    return status;
}

void a7680c_power_on(void) {
    LOGI(TAG, "Powering on A7680C...");
    /* Ensure the module is not held in reset before pulsing PWRKEY. */
    HAL_GPIO_WritePin(A7680C_RST_GPIO_Port, A7680C_RST_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(A7680C_PWR_GPIO_Port, A7680C_PWR_Pin, GPIO_PIN_RESET);
    osDelay(1200);
    HAL_GPIO_WritePin(A7680C_PWR_GPIO_Port, A7680C_PWR_Pin, GPIO_PIN_SET);
    osDelay(A7680C_BOOT_TIME_MS);
}

void a7680c_hard_reset(void) {
    LOGE(TAG, "LTE timeout, performing HARD RESET...");
    HAL_GPIO_WritePin(A7680C_RST_GPIO_Port, A7680C_RST_Pin, GPIO_PIN_RESET);
    osDelay(250);
    HAL_GPIO_WritePin(A7680C_RST_GPIO_Port, A7680C_RST_Pin, GPIO_PIN_SET);
    osDelay(A7680C_BOOT_TIME_MS); /* Wait for module to turn off/restart */
}

int a7680c_wait_at_ready(void) {
    if (send_at_command("AT\r\n", "OK", 1000) == 0) {
        send_at_command("ATE0\r\n", "OK", 1000);
        return 0;
    }
    return -1;
}

int a7680c_wait_network_registration(void) {
    char response[128];
    static uint32_t registration_poll_count = 0;
    int registration_rejected = 0;

    memset(response, 0, sizeof(response));
    if (send_at_command_capture("AT+CPIN?\r\n", "OK", 3000,
                                response, sizeof(response)) != 0) {
        return -1;
    }
    LOGI(TAG, "SIM: %s", response);
    if (strstr(response, "+CPIN: READY") == NULL) {
        LOGW(TAG, "SIM is not ready");
        return 2;
    }

    registration_poll_count++;
    if ((registration_poll_count % 5U) == 1U) {
        (void)query_and_log("AT+CSQ\r\n", "Signal", 3000);
        (void)query_and_log("AT+COPS?\r\n", "Operator", 5000);
        (void)query_and_log("AT+CFUN?\r\n", "Function", 3000);
        (void)query_and_log("AT+CGATT?\r\n", "Packet attach", 3000);
    }

    /* Match the response prefix first, then inspect the registration state.
     * 0,0 is a valid response meaning "not registered yet"; it must not be
     * reported as an AT transport timeout. */
    const char *commands[] = {
        "AT+CREG?\r\n",
        "AT+CGREG?\r\n",
        "AT+CEREG?\r\n"
    };
    int registration_pending = 0;
    int response_received = 0;
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        memset(response, 0, sizeof(response));
        if (send_at_command_capture(commands[i], "OK", 3000,
                                    response, sizeof(response)) == 0) {
            response_received = 1;
            LOGI(TAG, "%s: %s", commands[i], response);
            if (strstr(response, ",1") != NULL ||
                strstr(response, ",5") != NULL) {
                return 0;
            }
            if (strstr(response, ",0") != NULL ||
                strstr(response, ",2") != NULL ||
                strstr(response, ",4") != NULL) {
                registration_pending = 1;
            }
            if (strstr(response, ",3") != NULL) {
                registration_rejected = 1;
            }
        }
    }
    if (registration_rejected) {
        LOGW(TAG, "Network registration rejected");
        return 2;
    }
    if (registration_pending) {
        LOGI(TAG, "Network registration pending");
        return 1;
    }
    return response_received ? 1 : -1;
}

int a7680c_setup_network(const char *apn) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT+CGDCONT=1,\"IP\",\"%s\"\r\n", apn);
    if (send_at_command(cmd, "OK", 2000) == 0) {
        return 0;
    }
    return -1;
}

int a7680c_dial_ppp(void) {
    if (send_at_command("ATD*99#\r\n", "CONNECT", 10000) == 0) {
        LOGI(TAG, "Entering PPP Data Mode");
        clear_rx_buffer();
        return 0;
    }
    return -1;
}
