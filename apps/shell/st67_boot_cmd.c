#include "main.h"
#include "cmsis_os2.h"
#include "stdio.h"
#include "shell.h"

int cmd_st67_boot(int argc, char **argv) {
    printf("\r\n=== ST67 Boot Test ===\r\n");
    
    // 1. Reset all pins
    printf("Setting EN=LOW, BOOT=LOW, WAKEUP=HIGH, CS=HIGH...\r\n");
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_1, GPIO_PIN_RESET); // EN (PD1)
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_2, GPIO_PIN_RESET); // BOOT (PD2)
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_3, GPIO_PIN_SET);   // WAKEUP (PD3)
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_7, GPIO_PIN_SET);   // CS (PD7)
    
    osDelay(200);
    
    // 2. Enable chip
    printf("Setting EN=HIGH. Waiting for RDY (PD0) to go HIGH (max 3 seconds)...\r\n");
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_1, GPIO_PIN_SET); // EN
    
    // 3. Poll RDY
    int rdy_went_high = 0;
    for(int i=0; i<300; i++) {
        if(HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_0) == GPIO_PIN_SET) { // RDY (PD0)
            printf("RDY went HIGH at %d ms!\r\n", i * 10);
            rdy_went_high = 1;
            break;
        }
        osDelay(10);
    }
    
    if(!rdy_went_high) {
        printf("ERROR: RDY never went HIGH.\r\n");
    }
    
    printf("=== Test Done ===\r\n");
    return 0;
}
