/**
 * @file data_broker.h
 * @brief Data Broker for routing telemetry to Cloud
 */
#ifndef __DATA_BROKER_H__
#define __DATA_BROKER_H__

#include <stdint.h>
#include <stddef.h>
#include "cJSON.h"

void data_broker_init(void);
void data_broker_task(void *params);
int data_broker_publish(const char *topic, cJSON *payload);

#endif /* __DATA_BROKER_H__ */
