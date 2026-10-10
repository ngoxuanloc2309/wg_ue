/**
 * @file bsp.c
 * @brief Board Support Package - GatewayV2 STM32H563
 */
#include "bsp.h"
#include "logger.h"

void board_init(void) {
    LOGI("SYS", "Board init");
}

void led_on(uint8_t led) {
    (void)led;
}

void led_off(uint8_t led) {
    (void)led;
}

void led_toggle(uint8_t led) {
    (void)led;
}
