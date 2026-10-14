/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include "main.h"
#include "app_config.h"
#include "cmsis_os2.h"
#include "eth.h"
#include "gpdma.h"
#include "icache.h"
#include "usart.h"
#include "octospi.h"
#include "spi.h"
#include "tim.h"
#include "usb.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void PeriphCommonClock_Config(void);
static void MPU_Config(void);
void MX_FREERTOS_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
#define SYSTEM_BOOTLOADER_ADDRESS 0x0BF97000

static void enter_bootloader_mode(void) {
  // printf("Gonna Jump to Application 0x%08X\r\n",APPLICATION_ADDRESS);
  void (*app_reset_handler)(void) = (void *)(*((volatile uint32_t *)(SYSTEM_BOOTLOADER_ADDRESS + 4U)));
  // __set_MSP(*(volatile uint32_t*) 0x08040000);
  // Turn OFF the Green Led to tell the user that Bootloader is not running
  // HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET );    //Green LED OFF
  __disable_irq();
  
  SysTick->CTRL = 0;
  SysTick->LOAD = 0;
  SysTick->VAL = 0;
  
  __set_MSP(*(volatile uint32_t *)SYSTEM_BOOTLOADER_ADDRESS);
  /* Jump to application */
  SCB->VTOR = SYSTEM_BOOTLOADER_ADDRESS;
  __enable_irq();
  app_reset_handler(); // call the app reset handler
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* Keep configurable faults separate so diagnostics report the original
   * memory/bus/usage fault instead of only the escalated HardFault. */
  SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk |
                SCB_SHCSR_BUSFAULTENA_Msk |
                SCB_SHCSR_USGFAULTENA_Msk;

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* Configure the peripherals common clocks */
  PeriphCommonClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_GPDMA1_Init();
  MX_LPUART1_UART_Init();
  MX_ICACHE_Init();
  MX_OCTOSPI1_Init();
  MX_SPI1_Init();
  MX_SPI3_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  MX_USART3_UART_Init();
  MX_USART6_UART_Init();
  MX_USART10_UART_Init();
  
#if GW_USB_NETIF_ENABLE
  MX_USB_TinyUSB_Init();
#endif
  MX_TIM7_Init();
  /* USER CODE BEGIN 2 */

  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();
  /* Call init function for freertos objects (in app_freertos.c) */
  MX_FREERTOS_Init();

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  enter_bootloader_mode();
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48|RCC_OSCILLATORTYPE_LSI
                              |RCC_OSCILLATORTYPE_CSI;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.CSIState = RCC_CSI_ON;
  RCC_OscInitStruct.CSICalibrationValue = RCC_CSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLL1_SOURCE_CSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 125;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 3;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1_VCIRANGE_2;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1_VCORANGE_WIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_PCLK3;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }

  /* USB FS requires a tightly-trimmed 48 MHz HSI48 clock.  The .ioc file
   * declares the CRS target, but the generated clock code did not configure
   * CRS, leaving HSI48 free-running and outside USB tolerance on some parts.
   * Use USB SOF as the synchronization source, matching the STM32H5 TinyUSB
   * board reference configuration. */
  __HAL_RCC_CRS_CLK_ENABLE();
  RCC_CRSInitTypeDef crs_config = {0};
  crs_config.Prescaler = RCC_CRS_SYNC_DIV1;
  crs_config.Source = RCC_CRS_SYNC_SOURCE_USB;
  crs_config.Polarity = RCC_CRS_SYNC_POLARITY_RISING;
  crs_config.ReloadValue = __HAL_RCC_CRS_RELOADVALUE_CALCULATE(48000000U, 1000U);
  crs_config.ErrorLimitValue = 34U;
  crs_config.HSI48CalibrationValue = 32U;
  HAL_RCCEx_CRSConfig(&crs_config);

  /** Configure the programming delay
  */
  __HAL_FLASH_SET_PROGRAM_DELAY(FLASH_PROGRAMMING_DELAY_2);
}

/**
  * @brief Peripherals Common Clock Configuration
  * @retval None
  */
void PeriphCommonClock_Config(void)
{
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

  /** Initializes the peripherals clock
  */
  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_CKPER;
  PeriphClkInitStruct.CkperClockSelection = RCC_CLKPSOURCE_HSI;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>

/**
 * @brief FreeRTOS stack overflow hook - prints offending task name via UART
 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    extern UART_HandleTypeDef hlpuart1;
    extern uint32_t ethernetif_get_link_checkpoint(void);
    extern uint32_t ethernetif_get_link_stack_hwm(void);
    const char msg[] = "\r\n*** STACK OVERFLOW: ";
    char detail[128];
    HAL_UART_Transmit(&hlpuart1, (uint8_t *)msg, sizeof(msg) - 1, 100);
    HAL_UART_Transmit(&hlpuart1, (uint8_t *)pcTaskName, strlen(pcTaskName), 100);
    int n = snprintf(detail, sizeof(detail),
                     " task=%p checkpoint=%08lX hwm=%lu PSP=%08lX PSPLIM=%08lX\r\n",
                     (void *)xTask,
                     (unsigned long)ethernetif_get_link_checkpoint(),
                     (unsigned long)ethernetif_get_link_stack_hwm(),
                     (unsigned long)__get_PSP(),
                     (unsigned long)__get_PSPLIM());
    if (n > 0) {
        HAL_UART_Transmit(&hlpuart1, (uint8_t *)detail, (uint16_t)n, 100);
    }
    __disable_irq();
    while (1) {}
}

/* An lwIP assertion is an internal invariant failure.  Returning from it lets
 * lwIP continue with inconsistent PCB/netconn state and turns the original
 * error into unrelated heap/stack corruption. */
void lwip_platform_assert_failed(const char *message, const char *file, int line)
{
    extern UART_HandleTypeDef hlpuart1;
    char detail[240];
    int n;

    __disable_irq();
    n = snprintf(detail, sizeof(detail),
                 "\r\nLWIP_ASSERT message=%s file=%s line=%d IPSR=%lu PSP=%08lX\r\n",
                 message != NULL ? message : "(null)",
                 file != NULL ? file : "(null)", line,
                 (unsigned long)__get_IPSR(),
                 (unsigned long)__get_PSP());
    if (n > 0) {
        HAL_UART_Transmit(&hlpuart1, (uint8_t *)detail, (uint16_t)n, 1000);
    }
    while (1) {}
}

/**
 * @brief FreeRTOS configASSERT diagnostic.
 * Keep this separate from Error_Handler so queue/mutex assertions identify
 * the actual expression and whether they occurred in an interrupt context.
 */
void freertos_assert_failed(const char *expr, const char *file,
                            int line, uintptr_t pc, uint32_t ipsr)
{
    extern UART_HandleTypeDef hlpuart1;
    char msg[240];
    int n = snprintf(msg, sizeof(msg),
                     "FREERTOS_ASSERT %s:%d expr=%s PC=%08lX IPSR=%lu\r\n",
                     file, line, expr,
                     (unsigned long)pc, (unsigned long)ipsr);
    __disable_irq();
    if (n > 0) {
        HAL_UART_Transmit(&hlpuart1, (uint8_t *)msg, (uint16_t)n, 1000);
    }
    while (1) {}
}
/* USER CODE END 4 */

 /* MPU Configuration */

void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};
  MPU_Attributes_InitTypeDef MPU_AttributesInit = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

  /** Initializes and configures the Region 0 and the memory to be protected
  */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x08FFF000;
  MPU_InitStruct.LimitAddress = 0x08FFFFFF;
  MPU_InitStruct.AttributesIndex = MPU_ATTRIBUTES_NUMBER0;
  MPU_InitStruct.AccessPermission = MPU_REGION_ALL_RO;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_NOT_SHAREABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /** Initializes and configures the Attribute 0 and the memory to be protected
  */
  MPU_AttributesInit.Number = MPU_ATTRIBUTES_NUMBER0;
  MPU_AttributesInit.Attributes = INNER_OUTER(MPU_NOT_CACHEABLE);

  HAL_MPU_ConfigMemoryAttributes(&MPU_AttributesInit);
  /* Enables the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM6 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
extern volatile uint32_t ulRunTimeCounter;

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM6)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */
  if (htim->Instance == TIM7)
  {
    ulRunTimeCounter++;
  }
  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* Diagnostic caller/context: this distinguishes a HAL SPI error callback,
   * an assertion, and a peripheral init failure. */
  __disable_irq();
  extern UART_HandleTypeDef hlpuart1;
  extern SPI_HandleTypeDef hspi1;
  char line[180];
  int n = snprintf(line, sizeof(line),
                   "ERROR_HANDLER PC=%08lX SPI_ERR=%08lX SPI_STATE=%d SR=%08lX CFG1=%08lX CFG2=%08lX\r\n",
                   (unsigned long)(uintptr_t)__builtin_return_address(0),
                   (unsigned long)hspi1.ErrorCode,
                   (int)hspi1.State,
                   (unsigned long)SPI1->SR,
                   (unsigned long)SPI1->CFG1,
                   (unsigned long)SPI1->CFG2);
  if (n > 0) {
    HAL_UART_Transmit(&hlpuart1, (uint8_t *)line, (uint16_t)n, 1000);
  }
  while(1) {}
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
