/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : app_freertos.c
  * Description        : FreeRTOS applicative file
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "app_freertos.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "gateway.h"
#include "app_config.h"
#include "main.h"
#include "logger.h"
#include <string.h>
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

#define MONITOR_STACK_DEPTH_WORDS 512U
#define MONITOR_TASK_SNAPSHOT_MAX 32U
#define MONITOR_STACK_REPORT_CYCLES 10U

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  /* CMSIS-RTOS maps priority N to FreeRTOS priority N-1.  Normal (24)
   * therefore becomes FreeRTOS priority 23 and can be starved by the
   * application tasks. Keep the supervisor at the application baseline. */
  /* Keep the application supervisor in the same low-priority band as the
   * Datalogger application loop. Networking and protocol workers must block,
   * not be used to starve this task. */
  .priority = (osPriority_t) osPriorityLow,
  .stack_size = 1024 * 4
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
static void system_monitor_task(void *argument);

/* USER CODE END FunctionPrototypes */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);
  xTaskCreate(system_monitor_task, "MONITOR", MONITOR_STACK_DEPTH_WORDS, NULL,
              GW_TASK_PRIO_MONITOR, NULL);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}
/* USER CODE BEGIN Header_StartDefaultTask */
/**
* @brief Function implementing the defaultTask thread.
* @param argument: Not used
* @retval None
*/
volatile uint32_t ulRunTimeCounter = 0;
volatile uint32_t bootdbg_tim6_irq_count = 0;
volatile uint32_t bootdbg_tim7_irq_count = 0;
volatile uint32_t bootdbg_eth_irq_count = 0;
volatile uint32_t bootdbg_eth_wkup_irq_count = 0;
volatile uint32_t bootdbg_dma_irq_count[4] = {0};
volatile uint32_t bootdbg_usart1_irq_count = 0;
volatile uint32_t bootdbg_systick_count = 0;
volatile uint32_t bootdbg_tick_wake_count = 0;
extern volatile uint32_t shell_rx_irq_count;
extern volatile uint32_t shell_rx_byte_count;
extern volatile uint32_t shell_rx_queue_drop_count;

void configureTimerForRunTimeStats(void)
{
    extern TIM_HandleTypeDef htim7;
    HAL_TIM_Base_Start_IT(&htim7);
}

unsigned long getRunTimeCounterValue(void)
{
    return ulRunTimeCounter;
}

static int get_idle_cpu_percent(void)
{
  TaskHandle_t idle_task = xTaskGetHandle("IDLE");
  if(idle_task != NULL){
    int percent = ulTaskGetRunTimePercent(idle_task);
    return percent;
  }
  return 0;
}

static TaskStatus_t monitor_task_snapshot[MONITOR_TASK_SNAPSHOT_MAX];

static void monitor_report_task_stacks(void)
{
  UBaseType_t count = uxTaskGetSystemState(monitor_task_snapshot,
                                            MONITOR_TASK_SNAPSHOT_MAX,
                                            NULL);
  LOGI("MONITOR", "Task stack high-water report: tasks=%lu%s",
       (unsigned long)count,
       uxTaskGetNumberOfTasks() > MONITOR_TASK_SNAPSHOT_MAX ? " (truncated)" : "");
  for (UBaseType_t i = 0U; i < count; ++i) {
    LOGI("MONITOR", "Stack free: task=%s hwm=%luB",
         monitor_task_snapshot[i].pcTaskName,
         (unsigned long)(monitor_task_snapshot[i].usStackHighWaterMark *
                         sizeof(StackType_t)));
  }
}

static void system_monitor_task(void *argument)
{
  (void)argument;
  TickType_t last_wake = xTaskGetTickCount();
  uint32_t report_cycle = 0U;
  for (;;) {
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(GW_MONITOR_PERIOD_MS));

    uint32_t free_heap = xPortGetFreeHeapSize();
    uint32_t min_heap = xPortGetMinimumEverFreeHeapSize();
    UBaseType_t default_stack = defaultTaskHandle != NULL
                              ? uxTaskGetStackHighWaterMark(defaultTaskHandle) : 0U;
    UBaseType_t monitor_stack = uxTaskGetStackHighWaterMark(NULL);
    const uint32_t total_heap = configTOTAL_HEAP_SIZE;
    const uint32_t used_heap = total_heap - free_heap;
    const uint32_t default_stack_total = 1024U * 4U;
    const uint32_t monitor_stack_total =
        MONITOR_STACK_DEPTH_WORDS * sizeof(StackType_t);
    const uint32_t default_stack_free_bytes =
        (uint32_t)default_stack * sizeof(StackType_t);
    const uint32_t monitor_stack_free_bytes =
        (uint32_t)monitor_stack * sizeof(StackType_t);
    const uint32_t default_stack_used = default_stack_total > default_stack_free_bytes
        ? default_stack_total - default_stack_free_bytes : 0U;
    const uint32_t monitor_stack_used = monitor_stack_total > monitor_stack_free_bytes
        ? monitor_stack_total - monitor_stack_free_bytes : 0U;
    int idle_percent = get_idle_cpu_percent();
    LOGI("MONITOR",
         "System: CPU=%d%% | Heap used=%lu/%luKB (%lu%%), lowest free=%luKB | "
         "Stack used: defaultTask=%lu/%luB (%lu%%), monitor=%lu/%luB (%lu%%)",
         100 - idle_percent,
         (unsigned long)(used_heap / 1024U),
         (unsigned long)(total_heap / 1024U),
         (unsigned long)((used_heap * 100U) / total_heap),
         (unsigned long)(min_heap / 1024U),
         (unsigned long)default_stack_used,
         (unsigned long)default_stack_total,
         (unsigned long)((default_stack_used * 100U) / default_stack_total),
         (unsigned long)monitor_stack_used,
         (unsigned long)monitor_stack_total,
         (unsigned long)((monitor_stack_used * 100U) / monitor_stack_total));

    report_cycle++;
    if (report_cycle >= MONITOR_STACK_REPORT_CYCLES) {
      report_cycle = 0U;
      monitor_report_task_stacks();
    }
  }
}

/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN defaultTask */
  gateway_app_start();
  /* Infinite loop */
  for(;;)
  {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
  /* USER CODE END defaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

