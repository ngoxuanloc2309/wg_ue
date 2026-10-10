/**
 * @file st67w611_conf.h (MOCK for Portability Test)
 * @brief This is a fake hardware configuration to prove the driver builds
 *        without the GatewayV2 specific main.h or stm32h5xx_hal.h.
 */

#ifndef ST67W611_CONF_H
#define ST67W611_CONF_H

/* Use a different MCU family than GatewayV2 to prove portability */
#define ST67_USE_STM32F4
#include "stm32f4xx_hal.h"

/* Mock SPI Handle */
extern SPI_HandleTypeDef hspi3;
#define ST67_SPI_HANDLE (&hspi3)

/* Mock GPIO Ports and Pins (Fake values, completely decoupled from GatewayV2) */
#define ST67_NCS_PORT       GPIOB
#define ST67_NCS_PIN        GPIO_PIN_0

#define ST67_EN_PORT        GPIOB
#define ST67_EN_PIN         GPIO_PIN_1

#define ST67_BOOT_PORT      GPIOB
#define ST67_BOOT_PIN       GPIO_PIN_2

#define ST67_WAKEUP_PORT    GPIOB
#define ST67_WAKEUP_PIN     GPIO_PIN_3

#define ST67_RDY_PORT       GPIOB
#define ST67_RDY_PIN        GPIO_PIN_4

/* Mock EXTI IRQ handler definition */
extern void ST67_EXTI_IRQHandler(void);

#endif /* ST67W611_CONF_H */
