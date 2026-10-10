/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gpio.c
  * @brief   This file provides code for the configuration
  *          of all used GPIO pins.
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
#include "gpio.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/*----------------------------------------------------------------------------*/
/* Configure GPIO                                                             */
/*----------------------------------------------------------------------------*/
/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

/** Configure pins
     PC14-OSC32_IN(OSC32_IN)   ------> RCC_OSC32_IN
     PC15-OSC32_OUT(OSC32_OUT)   ------> RCC_OSC32_OUT
     PH0-OSC_IN(PH0)   ------> RCC_OSC_IN
     PH1-OSC_OUT(PH1)   ------> RCC_OSC_OUT
     PA13(JTMS/SWDIO)   ------> DEBUG_JTMS-SWDIO
     PA14(JTCK/SWCLK)   ------> DEBUG_JTCK-SWCLK
*/
void MX_GPIO_Init(void)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /* USB FS data pins: PA11 = USB_DM, PA12 = USB_DP. */
  GPIO_InitStruct.Pin = USB_N_Pin|USB_P_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF10_USB;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOE, USART10_DE_Pin|LED_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOD, USART3_DE_Pin|ST67_EN_Pin|ST67_BOOT_Pin|ST67_WAKEUP_Pin
                          |ST67_32K_Pin|ST67_NCS_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(USART6_DE_GPIO_Port, USART6_DE_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  /* A7680C reset is active-low; keep PWRKEY inactive and release RESET
   * during startup.  Leaving RESET low here makes the first AT handshake
   * impossible until the recovery path toggles it high. */
  HAL_GPIO_WritePin(A7680C_PWR_GPIO_Port, A7680C_PWR_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(A7680C_RST_GPIO_Port, A7680C_RST_Pin, GPIO_PIN_SET);

  /*Configure GPIO pins : USART10_DE_Pin LED_Pin */
  GPIO_InitStruct.Pin = USART10_DE_Pin|LED_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /*Configure GPIO pin : PA4_Pin */
  GPIO_InitStruct.Pin = PA4_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(PA4_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : USART3_DE_Pin ST67_EN_Pin ST67_BOOT_Pin ST67_WAKEUP_Pin
                           ST67_32K_Pin ST67_NCS_Pin */
  GPIO_InitStruct.Pin = USART3_DE_Pin|ST67_EN_Pin|ST67_BOOT_Pin|ST67_WAKEUP_Pin
                          |ST67_32K_Pin|ST67_NCS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /*Configure GPIO pin : USART6_DE_Pin */
  GPIO_InitStruct.Pin = USART6_DE_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(USART6_DE_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : ST67_RDY_Pin */
  GPIO_InitStruct.Pin = ST67_RDY_Pin;
  /* ST67 SPI protocol uses both edges:
   * rising = transaction/data ready, falling = header acknowledge. */
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(ST67_RDY_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : A7680C_PWR_Pin A7680C_RST_Pin */
  GPIO_InitStruct.Pin = A7680C_PWR_Pin|A7680C_RST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI0_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(EXTI0_IRQn);

}

/* USER CODE BEGIN 2 */

/* USER CODE END 2 */
