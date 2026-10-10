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
#include "icache.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

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
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* GatewayV2 schematic mapping. These are GPIOs in the runtime bypass build;
 * the CubeMX USART2 assignment is retained for pin/documentation reference. */
#define ST67_UART_TX_Pin GPIO_PIN_5 /* STM32 TX -> ST67 UART_RX */
#define ST67_UART_RX_Pin GPIO_PIN_6 /* STM32 RX <- ST67 UART_TX */

#ifdef NCP_AUTO_MODE
#if !defined(__OPTIMIZE__)
#error "NCP_AUTO_MODE requires an optimized build (Release/-O2 or better)"
#endif
#endif

#ifndef NCP_AUTO_MODE
static void NCP_IsolateControlPins(void)
{
  GPIO_InitTypeDef gpio = {0};

  /* Manual BOOT/CHIP_EN mode: the external user controls both signals.
   * Keep the STM32 high-Z to avoid contention with jumpers or an external
   * controller connected to the ST67 pins. */
  gpio.Pin = ST67_EN_Pin | ST67_BOOT_Pin;
  gpio.Mode = GPIO_MODE_ANALOG;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOD, &gpio);
}

static void NCP_IsolateUartPins(void)
{
  GPIO_InitTypeDef gpio = {0};

  /* Direct USB-TTL mode: the STM32 must not electrically participate in
   * either UART bus. Analog mode is the highest-impedance GPIO state and
   * also prevents an accidental alternate-function output from driving a
   * line connected to the external adapter or the ST67. */
  gpio.Mode = GPIO_MODE_ANALOG;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;

  gpio.Pin = DEBUG_TX_Pin | DEBUG_RX_Pin;
  HAL_GPIO_Init(GPIOA, &gpio);

  gpio.Pin = ST67_UART_TX_Pin | ST67_UART_RX_Pin;
  HAL_GPIO_Init(GPIOD, &gpio);

  /* These peripherals are intentionally unused in the manual flashing
   * build. Keep them disabled even if the CubeMX configuration changes. */
  __HAL_RCC_LPUART1_CLK_DISABLE();
  __HAL_RCC_USART2_CLK_DISABLE();
}
#endif

#ifdef NCP_AUTO_MODE
static void NCP_ConfigureAutoUartPins(void)
{
  GPIO_InitTypeDef gpio = {0};

  /* ST reference mapping, adapted to GatewayV2:
   *   PA10 (debug RX)  -> PD5 (ST67 RX / MCU TX)
   *   PD6  (ST67 TX)   -> PA9  (debug TX / MCU TX)
   * Use GPIO bit-copy instead of HAL UART, DMA, or interrupts. */
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;

  gpio.Pin = DEBUG_TX_Pin;
  HAL_GPIO_Init(GPIOA, &gpio);
  gpio.Pin = ST67_UART_TX_Pin;
  HAL_GPIO_Init(GPIOD, &gpio);

  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  gpio.Pin = DEBUG_RX_Pin;
  HAL_GPIO_Init(GPIOA, &gpio);
  gpio.Pin = ST67_UART_RX_Pin;
  HAL_GPIO_Init(GPIOD, &gpio);

  /* UART idle level is high. */
  GPIOA->BSRR = DEBUG_TX_Pin;
  GPIOD->BSRR = ST67_UART_TX_Pin;
}

static void NCP_AutoEnterFlashMode(void)
{
  /* BOOT must be high when CHIP_EN is released. */
  HAL_GPIO_WritePin(ST67_BOOT_GPIO_Port, ST67_BOOT_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(ST67_EN_GPIO_Port, ST67_EN_Pin, GPIO_PIN_RESET);
  HAL_Delay(100);
  HAL_GPIO_WritePin(ST67_EN_GPIO_Port, ST67_EN_Pin, GPIO_PIN_SET);
  HAL_Delay(100);
  HAL_GPIO_WritePin(ST67_BOOT_GPIO_Port, ST67_BOOT_Pin, GPIO_PIN_RESET);
}

__attribute__((always_inline)) static inline void NCP_AutoUartPassThrough(void)
{
  /* PA10 -> PD5 and PD6 -> PA9. Keep this loop free of function calls. */
  if ((GPIOA->IDR & DEBUG_RX_Pin) != 0U)
    GPIOD->BSRR = ST67_UART_TX_Pin;
  else
    GPIOD->BSRR = (uint32_t)ST67_UART_TX_Pin << 16U;

  if ((GPIOD->IDR & ST67_UART_RX_Pin) != 0U)
    GPIOA->BSRR = DEBUG_TX_Pin;
  else
    GPIOA->BSRR = (uint32_t)DEBUG_TX_Pin << 16U;
}
#endif

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

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_ICACHE_Init();
  /* USER CODE BEGIN 2 */

 #ifdef NCP_AUTO_MODE
  NCP_ConfigureAutoUartPins();
  NCP_AutoEnterFlashMode();
  /* QConn uses 2 Mbps. Prevent SysTick/HAL or other interrupts from
   * stretching the GPIO bit-copy loop and corrupting UART frames. */
  __disable_irq();
  while (1)
  {
    NCP_AutoUartPassThrough();
  }
 #else
  NCP_IsolateUartPins();
  NCP_IsolateControlPins();
  /* The user controls BOOT/CHIP_EN externally and the USB-TTL adapter owns
   * the ST67 UART lines. The STM32 remains electrically passive. */
  while (1)
  {
  }
 #endif

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
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
  /* Match the official NUCLEO-H563ZI NCP loader clock. */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLL1_SOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 31;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1_VCIRANGE_3;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1_VCORANGE_WIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 2048;
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

  /** Configure the programming delay
  */
  __HAL_FLASH_SET_PROGRAM_DELAY(FLASH_PROGRAMMING_DELAY_2);
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
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
