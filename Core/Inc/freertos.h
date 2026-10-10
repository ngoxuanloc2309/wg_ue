/**
 * @file freertos.h
 * @brief FreeRTOS include redirect - maps to Middlewares path
 */
#ifndef FREERTOS_H
#define FREERTOS_H

/* Redirect includes to Middlewares path */
#include "../../Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "../../Middlewares/Third_Party/FreeRTOS/Source/include/task.h"
#include "../../Middlewares/Third_Party/FreeRTOS/Source/include/queue.h"
#include "../../Middlewares/Third_Party/FreeRTOS/Source/include/semphr.h"
#include "../../Middlewares/Third_Party/FreeRTOS/Source/include/event_groups.h"
#include "../../Middlewares/Third_Party/FreeRTOS/Source/include/timers.h"

#endif /* FREERTOS_H */
