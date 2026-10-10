/**
 * @file mongoose_glue.h
 * @brief Mongoose glue code header
 */
#ifndef __MONGOOSE_GLUE_H__
#define __MONGOOSE_GLUE_H__

#include "mongoose.h"
#include "lwip/netif.h"

/**
 * @brief Custom millisecond timer for Mongoose
 */
uint64_t mongoose_get_time_ms(void);

/**
 * @brief Initialize mongoose with lwIP
 * @param mgr: mongoose manager
 * @param netif: lwIP network interface
 */
void mongoose_net_init(struct mg_mgr *mgr, struct netif *netif);

#endif /* __MONGOOSE_GLUE_H__ */
