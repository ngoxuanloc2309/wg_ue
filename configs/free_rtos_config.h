/**
 * @file free_rtos_config.h
 * @brief FreeRTOS Configuration
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#define configUSE_PREEMPTION            1
#define configSUPPORT_STATIC_ALLOCATION 1
#define configUSE_IDLE_HOOK             0
#define configUSE_TICK_HOOK             0
#define configCPU_CLOCK_HZ              (240000000)
#define configSYSTICK_CLOCK_HZ          (240000000)
#define configMAX_PRIORITIES            10
#define configMINIMAL_STACK_SIZE        (256)
#define configTOTAL_HEAP_SIZE           (64 * 1024)
#define configMAX_TASK_NAME_LEN         16
#define configUSE_TRACE_FACILITY        1
#define configUSE_STATS_FORMATTING_FUNCTIONS 1
#define configUSE_MUTEXES               1
#define configQUEUE_REGISTRY_SIZE       8
#define configENABLE_BACKWARD_COMPATIBILITY   1

#define INCLUDE_vTaskDelete             1
#define INCLUDE_vTaskDelayUntil         1
#define INCLUDE_xTaskAbortDelay         1

#endif /* FREERTOS_CONFIG_H */
