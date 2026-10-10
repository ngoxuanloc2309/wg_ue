/**
 * @file mongoose_glue.c
 * @brief Mongoose glue code - lwIP integration
 */
#include "mongoose_glue.h"
#include "mongoose.h"
#include "logger.h"

/**
 * @brief Custom millisecond timer for Mongoose
 */
uint64_t mongoose_get_time_ms(void) {
    return HAL_GetTick();
}

/**
 * @brief Initialize mongoose with lwIP
 * @param mgr: mongoose manager
 * @param netif: lwIP network interface
 */
void mongoose_net_init(struct mg_mgr *mgr, struct netif *netif) {
    (void)mgr;
    (void)netif;
    LOGI("SYS", "Mongoose network init (using lwIP sockets)");
}
