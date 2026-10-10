/**
 * @file bsp.h
 * @brief Board Support Package header
 */
#ifndef __BSP_H__
#define __BSP_H__

#include <stdint.h>

void board_init(void);
void led_on(uint8_t led);
void led_off(uint8_t led);
void led_toggle(uint8_t led);

#endif /* __BSP_H__ */
