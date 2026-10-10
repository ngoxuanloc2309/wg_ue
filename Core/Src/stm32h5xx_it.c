/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32h5xx_it.c
  * @brief   Interrupt Service Routines.
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
#include "usb.h"
#include "stm32h5xx_it.h"
#include "usart.h"
#include <stdio.h>
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "tusb.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

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
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* External variables --------------------------------------------------------*/
extern ETH_HandleTypeDef heth;
extern DMA_HandleTypeDef handle_GPDMA1_Channel3;
extern DMA_HandleTypeDef handle_GPDMA1_Channel2;
extern DMA_HandleTypeDef handle_GPDMA1_Channel1;
extern DMA_HandleTypeDef handle_GPDMA1_Channel0;
extern UART_HandleTypeDef huart1;
extern SPI_HandleTypeDef hspi1;
extern TIM_HandleTypeDef htim7;
extern TIM_HandleTypeDef htim6;
extern volatile uint32_t bootdbg_tim6_irq_count;
extern volatile uint32_t bootdbg_tim7_irq_count;
extern volatile uint32_t bootdbg_eth_irq_count;
extern volatile uint32_t bootdbg_eth_wkup_irq_count;
extern volatile uint32_t bootdbg_dma_irq_count[4];
extern volatile uint32_t bootdbg_usart1_irq_count;
extern volatile uint32_t bootdbg_systick_count;
extern volatile uint32_t bootdbg_tick_wake_count;

/* USER CODE BEGIN EV */

/* USER CODE END EV */

/******************************************************************************/
/*           Cortex Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
  * @brief This function handles Non maskable interrupt.
  */
void NMI_Handler(void)
{
  /* USER CODE BEGIN NonMaskableInt_IRQn 0 */

  /* USER CODE END NonMaskableInt_IRQn 0 */
  /* USER CODE BEGIN NonMaskableInt_IRQn 1 */
   while (1)
  {
  }
  /* USER CODE END NonMaskableInt_IRQn 1 */
}

/**
  * @brief This function handles Hard fault interrupt.
  */
__attribute__((used, noreturn)) static void fault_dump(const char *name,
                                                        uint32_t *stack,
                                                        uint32_t exc_return)
{
  char line[320];
  uint32_t *frame = NULL;
  uint32_t frame_offset = 0xFFFFFFFFU;

  /* Cortex-M33 exception frames vary with FP lazy stacking and security
   * state.  Do not blindly add 18 words based only on EXC_RETURN: find the
   * basic frame by requiring a Thumb xPSR and a PC in internal flash. */
  if (((uintptr_t)stack >= 0x20000000UL) &&
      ((uintptr_t)stack < 0x20100000UL)) {
    for (uint32_t i = 0U; i <= 24U; ++i) {
      uint32_t pc = stack[i + 6U];
      uint32_t xpsr = stack[i + 7U];
      if ((pc >= 0x08000000UL) && (pc < 0x08200000UL) &&
          ((xpsr & (1UL << 24)) != 0U)) {
        frame = &stack[i];
        frame_offset = i;
        break;
      }
    }
  }

  uint32_t pc = frame != NULL ? frame[6] : 0xFFFFFFFFU;
  uint32_t lr = frame != NULL ? frame[5] : 0xFFFFFFFFU;
  uint32_t xpsr = frame != NULL ? frame[7] : 0xFFFFFFFFU;
  int n = snprintf(line, sizeof(line),
                   "\r\n%s PC=%08lX LR=%08lX xPSR=%08lX frame=%lu SP=%08lX EXC=%08lX CONTROL=%08lX MSP=%08lX PSP=%08lX CFSR=%08lX HFSR=%08lX BFAR=%08lX MMFAR=%08lX\r\n",
                   name,
                   (unsigned long)pc, (unsigned long)lr,
                   (unsigned long)xpsr, (unsigned long)frame_offset,
                   (unsigned long)(uintptr_t)stack,
                   (unsigned long)exc_return,
                   (unsigned long)__get_CONTROL(),
                   (unsigned long)__get_MSP(),
                   (unsigned long)__get_PSP(),
                   (unsigned long)SCB->CFSR, (unsigned long)SCB->HFSR,
                   (unsigned long)SCB->BFAR, (unsigned long)SCB->MMFAR);
  if (n > 0) {
    HAL_UART_Transmit(&hlpuart1, (uint8_t *)line, (uint16_t)n, 1000);
  }
  if (((uintptr_t)stack >= 0x20000000UL) &&
      ((uintptr_t)stack < 0x20100000UL)) {
    n = snprintf(line, sizeof(line),
                 "RAW %08lX %08lX %08lX %08lX %08lX %08lX %08lX %08lX %08lX %08lX %08lX %08lX %08lX %08lX %08lX %08lX\r\n",
                 (unsigned long)stack[0], (unsigned long)stack[1],
                 (unsigned long)stack[2], (unsigned long)stack[3],
                 (unsigned long)stack[4], (unsigned long)stack[5],
                 (unsigned long)stack[6], (unsigned long)stack[7],
                 (unsigned long)stack[8], (unsigned long)stack[9],
                 (unsigned long)stack[10], (unsigned long)stack[11],
                 (unsigned long)stack[12], (unsigned long)stack[13],
                 (unsigned long)stack[14], (unsigned long)stack[15]);
    if (n > 0) {
      HAL_UART_Transmit(&hlpuart1, (uint8_t *)line, (uint16_t)n, 1000);
    }
  }
  while (1) { }
}

__attribute__((used, noreturn)) static void hardfault_dump(uint32_t *stack, uint32_t exc_return)
{
  fault_dump("HARDFAULT", stack, exc_return);
}

__attribute__((used, noreturn)) static void memfault_dump(uint32_t *stack, uint32_t exc_return)
{
  fault_dump("MEMFAULT", stack, exc_return);
}

__attribute__((used, noreturn)) static void busfault_dump(uint32_t *stack, uint32_t exc_return)
{
  fault_dump("BUSFAULT", stack, exc_return);
}

__attribute__((used, noreturn)) static void usagefault_dump(uint32_t *stack, uint32_t exc_return)
{
  fault_dump("USAGEFAULT", stack, exc_return);
}

__attribute__((naked)) void HardFault_Handler(void)
{
  __asm volatile (
    "tst lr, #4                \n"
    "ite eq                    \n"
    "mrseq r0, msp             \n"
    "mrsne r0, psp             \n"
    "mov r1, lr                \n"
    "b hardfault_dump          \n");
}

/**
  * @brief This function handles Memory management fault.
  */
__attribute__((naked)) void MemManage_Handler(void)
{
  __asm volatile (
    "tst lr, #4                \n"
    "ite eq                    \n"
    "mrseq r0, msp             \n"
    "mrsne r0, psp             \n"
    "mov r1, lr                \n"
    "b memfault_dump           \n");
}

/**
  * @brief This function handles Pre-fetch fault, memory access fault.
  */
__attribute__((naked)) void BusFault_Handler(void)
{
  __asm volatile (
    "tst lr, #4                \n"
    "ite eq                    \n"
    "mrseq r0, msp             \n"
    "mrsne r0, psp             \n"
    "mov r1, lr                \n"
    "b busfault_dump           \n");
}

/**
  * @brief This function handles Undefined instruction or illegal state.
  */
__attribute__((naked)) void UsageFault_Handler(void)
{
  __asm volatile (
    "tst lr, #4                \n"
    "ite eq                    \n"
    "mrseq r0, msp             \n"
    "mrsne r0, psp             \n"
    "mov r1, lr                \n"
    "b usagefault_dump         \n");
}

/**
  * @brief This function handles Debug monitor.
  */
void DebugMon_Handler(void)
{
  /* USER CODE BEGIN DebugMonitor_IRQn 0 */

  /* USER CODE END DebugMonitor_IRQn 0 */
  /* USER CODE BEGIN DebugMonitor_IRQn 1 */

  /* USER CODE END DebugMonitor_IRQn 1 */
}

/******************************************************************************/
/* STM32H5xx Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file (startup_stm32h5xx.s).                    */
/******************************************************************************/

/**
  * @brief This function handles EXTI Line0 interrupt.
  */
void EXTI0_IRQHandler(void)
{
  /* USER CODE BEGIN EXTI0_IRQn 0 */

  /* USER CODE END EXTI0_IRQn 0 */
  HAL_GPIO_EXTI_IRQHandler(ST67_RDY_Pin);
  /* USER CODE BEGIN EXTI0_IRQn 1 */

  /* USER CODE END EXTI0_IRQn 1 */
}

/**
  * @brief This function handles GPDMA1 Channel 0 global interrupt.
  */
void GPDMA1_Channel0_IRQHandler(void)
{
  bootdbg_dma_irq_count[0]++;
  /* USER CODE BEGIN GPDMA1_Channel0_IRQn 0 */

  /* USER CODE END GPDMA1_Channel0_IRQn 0 */
  HAL_DMA_IRQHandler(&handle_GPDMA1_Channel0);
  /* USER CODE BEGIN GPDMA1_Channel0_IRQn 1 */

  /* USER CODE END GPDMA1_Channel0_IRQn 1 */
}

/**
  * @brief This function handles GPDMA1 Channel 1 global interrupt.
  */
void GPDMA1_Channel1_IRQHandler(void)
{
  bootdbg_dma_irq_count[1]++;
  /* USER CODE BEGIN GPDMA1_Channel1_IRQn 0 */

  /* USER CODE END GPDMA1_Channel1_IRQn 0 */
  HAL_DMA_IRQHandler(&handle_GPDMA1_Channel1);
  /* USER CODE BEGIN GPDMA1_Channel1_IRQn 1 */

  /* USER CODE END GPDMA1_Channel1_IRQn 1 */
}

/**
  * @brief This function handles GPDMA1 Channel 2 global interrupt.
  */
void GPDMA1_Channel2_IRQHandler(void)
{
  bootdbg_dma_irq_count[2]++;
  /* USER CODE BEGIN GPDMA1_Channel2_IRQn 0 */

  /* USER CODE END GPDMA1_Channel2_IRQn 0 */
  HAL_DMA_IRQHandler(&handle_GPDMA1_Channel2);
  /* USER CODE BEGIN GPDMA1_Channel2_IRQn 1 */

  /* USER CODE END GPDMA1_Channel2_IRQn 1 */
}

/**
  * @brief This function handles GPDMA1 Channel 3 global interrupt.
  */
void GPDMA1_Channel3_IRQHandler(void)
{
  bootdbg_dma_irq_count[3]++;
  /* USER CODE BEGIN GPDMA1_Channel3_IRQn 0 */

  /* USER CODE END GPDMA1_Channel3_IRQn 0 */
  HAL_DMA_IRQHandler(&handle_GPDMA1_Channel3);
  /* USER CODE BEGIN GPDMA1_Channel3_IRQn 1 */

  /* USER CODE END GPDMA1_Channel3_IRQn 1 */
}

/**
  * @brief This function handles TIM6 global interrupt.
  */
void TIM6_IRQHandler(void)
{
  bootdbg_tim6_irq_count++;
  /* USER CODE BEGIN TIM6_IRQn 0 */

  /* USER CODE END TIM6_IRQn 0 */
  HAL_TIM_IRQHandler(&htim6);
  /* USER CODE BEGIN TIM6_IRQn 1 */

  /* USER CODE END TIM6_IRQn 1 */
}

/**
  * @brief This function handles TIM7 global interrupt.
  */
void TIM7_IRQHandler(void)
{
  bootdbg_tim7_irq_count++;
  /* USER CODE BEGIN TIM7_IRQn 0 */

  /* USER CODE END TIM7_IRQn 0 */
  HAL_TIM_IRQHandler(&htim7);
  /* USER CODE BEGIN TIM7_IRQn 1 */

  /* USER CODE END TIM7_IRQn 1 */
}

/**
  * @brief This function handles SPI1 global interrupt.
  */
void SPI1_IRQHandler(void)
{
  /* USER CODE BEGIN SPI1_IRQn 0 */

  /* USER CODE END SPI1_IRQn 0 */
  HAL_SPI_IRQHandler(&hspi1);
  /* USER CODE BEGIN SPI1_IRQn 1 */

  /* USER CODE END SPI1_IRQn 1 */
}

/**
  * @brief This function handles USART1 global interrupt.
  */
void USART1_IRQHandler(void)
{
  bootdbg_usart1_irq_count++;
  /* USER CODE BEGIN USART1_IRQn 0 */

  /* USER CODE END USART1_IRQn 0 */
  HAL_UART_IRQHandler(&huart1);
  /* USER CODE BEGIN USART1_IRQn 1 */

  /* USER CODE END USART1_IRQn 1 */
}

/**
  * @brief This function handles LPUART1 global interrupt.
  */
void LPUART1_IRQHandler(void)
{
  HAL_UART_IRQHandler(&hlpuart1);
}

/**
  * @brief This function handles Ethernet global interrupt.
  */
void ETH_IRQHandler(void)
{
  bootdbg_eth_irq_count++;
  /* USER CODE BEGIN ETH_IRQn 0 */

  /* USER CODE END ETH_IRQn 0 */
  HAL_ETH_IRQHandler(&heth);
  /* USER CODE BEGIN ETH_IRQn 1 */

  /* USER CODE END ETH_IRQn 1 */
}

/**
  * @brief This function handles Ethernet Wakeup global interrupt.
  */
void ETH_WKUP_IRQHandler(void)
{
  bootdbg_eth_wkup_irq_count++;
  /* USER CODE BEGIN ETH_WKUP_IRQn 0 */

  /* USER CODE END ETH_WKUP_IRQn 0 */
  HAL_ETH_IRQHandler(&heth);
  /* USER CODE BEGIN ETH_WKUP_IRQn 1 */

  /* USER CODE END ETH_WKUP_IRQn 1 */
}

/* USER CODE BEGIN 1 */
/**
  * @brief This function handles USB FS global interrupt.
  */
void USB_DRD_FS_IRQHandler(void)
{
  g_usb_irq_count++;
  g_usb_last_istr = USB_DRD_FS->ISTR;
  /* USER CODE BEGIN USB_DRD_FS_IRQn 0 */

  /* USER CODE END USB_DRD_FS_IRQn 0 */
  /* The NVIC is enabled during board setup, before TinyUSB is initialized.
   * Do not enter the DCD with an uninitialized stack on an early/spurious
   * interrupt. */
  if (g_usb_tusb_initialized != 0U) {
    tud_int_handler(0);
  }
  /* USER CODE BEGIN USB_DRD_FS_IRQn 1 */

  /* USER CODE END USB_DRD_FS_IRQn 1 */
}
/* USER CODE END 1 */
