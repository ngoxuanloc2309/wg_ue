/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32h5xx_hal.h"
#include <stdint.h>

/* Shared console TX path; implemented by the gateway application. */
void serial_tx_write(const uint8_t *data, uint16_t length, uint32_t timeout);

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define USART10_DE_Pin GPIO_PIN_4
#define USART10_DE_GPIO_Port GPIOE
#define OCTOSPI_IO2_Pin GPIO_PIN_2
#define OCTOSPI_IO2_GPIO_Port GPIOC
#define OCTOSPI_CLK_Pin GPIO_PIN_3
#define OCTOSPI_CLK_GPIO_Port GPIOA
#define PA4_Pin GPIO_PIN_4
#define PA4_GPIO_Port GPIOA
#define OCTOSPI_IO3_Pin GPIO_PIN_6
#define OCTOSPI_IO3_GPIO_Port GPIOA
#define OCTOSPI_IO1_Pin GPIO_PIN_0
#define OCTOSPI_IO1_GPIO_Port GPIOB
#define OCTOSPI_IO0_Pin GPIO_PIN_1
#define OCTOSPI_IO0_GPIO_Port GPIOB
#define OCTOSPI_DQS_Pin GPIO_PIN_2
#define OCTOSPI_DQS_GPIO_Port GPIOB
#define OCTOSPI_IO4_Pin GPIO_PIN_7
#define OCTOSPI_IO4_GPIO_Port GPIOE
#define OCTOSPI_IO5_Pin GPIO_PIN_8
#define OCTOSPI_IO5_GPIO_Port GPIOE
#define OCTOSPI_IO6_Pin GPIO_PIN_9
#define OCTOSPI_IO6_GPIO_Port GPIOE
#define OCTOSPI_IO7_Pin GPIO_PIN_10
#define OCTOSPI_IO7_GPIO_Port GPIOE
#define OCTOSPI_NCS_Pin GPIO_PIN_11
#define OCTOSPI_NCS_GPIO_Port GPIOE
#define USART3_DE_Pin GPIO_PIN_10
#define USART3_DE_GPIO_Port GPIOD
#define USART6_DE_Pin GPIO_PIN_8
#define USART6_DE_GPIO_Port GPIOC
#define DEBUG_TX_Pin GPIO_PIN_9
#define DEBUG_TX_GPIO_Port GPIOA
#define DEBUG_RX_Pin GPIO_PIN_10
#define DEBUG_RX_GPIO_Port GPIOA
#define USB_N_Pin GPIO_PIN_11
#define USB_N_GPIO_Port GPIOA
#define USB_P_Pin GPIO_PIN_12
#define USB_P_GPIO_Port GPIOA
#define SWDIO_Pin GPIO_PIN_13
#define SWDIO_GPIO_Port GPIOA
#define SWCLK_Pin GPIO_PIN_14
#define SWCLK_GPIO_Port GPIOA
#define SPI_NSS_Pin GPIO_PIN_15
#define SPI_NSS_GPIO_Port GPIOA
#define SPI_SCK_Pin GPIO_PIN_10
#define SPI_SCK_GPIO_Port GPIOC
#define SPI_MISO_Pin GPIO_PIN_11
#define SPI_MISO_GPIO_Port GPIOC
#define SPI_MOSI_Pin GPIO_PIN_12
#define SPI_MOSI_GPIO_Port GPIOC
#define ST67_RDY_Pin GPIO_PIN_0
#define ST67_RDY_GPIO_Port GPIOD
#define ST67_RDY_EXTI_IRQn EXTI0_IRQn
#define ST67_EN_Pin GPIO_PIN_1
#define ST67_EN_GPIO_Port GPIOD
#define ST67_BOOT_Pin GPIO_PIN_2
#define ST67_BOOT_GPIO_Port GPIOD
#define ST67_WAKEUP_Pin GPIO_PIN_3
#define ST67_WAKEUP_GPIO_Port GPIOD
#define ST67_32K_Pin GPIO_PIN_4
#define ST67_32K_GPIO_Port GPIOD
#define ST67_TX_Pin GPIO_PIN_5
#define ST67_TX_GPIO_Port GPIOD
#define ST67_RX_Pin GPIO_PIN_6
#define ST67_RX_GPIO_Port GPIOD
#define ST67_NCS_Pin GPIO_PIN_7
#define ST67_NCS_GPIO_Port GPIOD
#define ST67_SCK_Pin GPIO_PIN_3
#define ST67_SCK_GPIO_Port GPIOB
#define ST67_MISO_Pin GPIO_PIN_4
#define ST67_MISO_GPIO_Port GPIOB
#define ST67_MOSI_Pin GPIO_PIN_5
#define ST67_MOSI_GPIO_Port GPIOB
#define A7680C_RX_Pin GPIO_PIN_6
#define A7680C_RX_GPIO_Port GPIOB
#define A7680C_TX_Pin GPIO_PIN_7
#define A7680C_TX_GPIO_Port GPIOB
#define A7680C_PWR_Pin GPIO_PIN_8
#define A7680C_PWR_GPIO_Port GPIOB
#define A7680C_RST_Pin GPIO_PIN_9
#define A7680C_RST_GPIO_Port GPIOB
#define LED_Pin GPIO_PIN_0
#define LED_GPIO_Port GPIOE

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
