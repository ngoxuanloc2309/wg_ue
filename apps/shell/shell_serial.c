#include "shell_serial.h"
#include "cli_shell.h"
#include "FreeRTOS.h"
#include "task.h"
#include "logger.h"
#include "usart.h"
#include "app_config.h"
#include <string.h>

extern UART_HandleTypeDef hlpuart1;
#define SHELL_UART (&hlpuart1)

static const char *TAG = "Shell";

TaskHandle_t shell_serial_task_handle = NULL;
ShellContext_t serial_shell;
volatile uint32_t shell_rx_irq_count = 0;
volatile uint32_t shell_rx_byte_count = 0;
volatile uint32_t shell_rx_queue_drop_count = 0;
static uint8_t shell_rx_byte;

int shell_send_char(void *arg, char c){
    serial_tx_write((const uint8_t *)&c, 1U, 10U);
    return c;
}

int shell_send_str(void *arg, char *str){
    serial_tx_write((const uint8_t *)str, (uint16_t)strlen(str), 100U);
    return strlen(str);
}

static QueueHandle_t shell_rx_queue = NULL;

/* HAL owns the UART IRQ. RX bytes are forwarded to the RTOS queue from the
 * common receive-complete callback, matching the Datalogger UART path. */
void shell_uart_rx_complete(UART_HandleTypeDef *huart)
{
    if (huart != SHELL_UART) {
        return;
    }

    BaseType_t higher_priority_task_woken = pdFALSE;
    shell_rx_irq_count++;
    shell_rx_byte_count++;
    if (shell_rx_queue == NULL ||
        xQueueSendFromISR(shell_rx_queue, &shell_rx_byte,
                          &higher_priority_task_woken) != pdTRUE) {
        shell_rx_queue_drop_count++;
    }
    (void)HAL_UART_Receive_IT(SHELL_UART, &shell_rx_byte, 1U);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

void shell_serial_get_diagnostics(shell_serial_diag_t *diag)
{
    if (diag != NULL) {
        diag->rx_irq_count = shell_rx_irq_count;
        diag->rx_byte_count = shell_rx_byte_count;
        diag->rx_queue_drop_count = shell_rx_queue_drop_count;
    }
}

int shell_recv_char(void *arg){
    uint8_t c = 0;
    if (shell_rx_queue != NULL) {
        if (xQueueReceive(shell_rx_queue, &c, portMAX_DELAY) == pdTRUE) {
            return c;
        }
    }
    return -1;
}

void shell_serial_task(void *arg){
    LOGI(TAG, "Serial shell task started on USART1");
    serial_shell.impl.send_char = shell_send_char;
    serial_shell.impl.send_str = shell_send_str;
    serial_shell.impl.receive_char = shell_recv_char;
    serial_shell.impl.arg = NULL;
    cli_shell_boot(&serial_shell);
    int c;
    while(1){
        c = shell_recv_char(NULL);
        if(c > 0){
            cli_shell_receive_char(&serial_shell, (char)c);
        }
    }
}

void shell_serial_init(void)
{
    extern void shell_commands_init(void);
    shell_commands_init();
    
    shell_rx_queue = xQueueCreate(64, sizeof(uint8_t));
    if (shell_rx_queue != NULL) {
        (void)HAL_UART_Receive_IT(SHELL_UART, &shell_rx_byte, 1U);
        HAL_NVIC_SetPriority(LPUART1_IRQn, 6, 0);
        HAL_NVIC_EnableIRQ(LPUART1_IRQn);
    }

    BaseType_t ret = xTaskCreate(shell_serial_task, "SHELL", GW_TASK_STACK_SHELL, NULL, GW_TASK_PRIO_SHELL, &shell_serial_task_handle);
    configASSERT(ret == pdPASS);
}
