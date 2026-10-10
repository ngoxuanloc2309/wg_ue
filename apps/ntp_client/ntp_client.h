#ifndef __NTP_CLIENT_H__
#define __NTP_CLIENT_H__

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Initialize NTP client
 */
void ntp_client_init(void);

/**
 * @brief Force NTP time sync
 */
void ntp_client_sync(void);

/**
 * @brief Check if time is synchronized
 */
bool ntp_is_synced(void);

/**
 * @brief Get current Unix timestamp (seconds since 1970)
 * @return Unix timestamp
 */
uint32_t ntp_get_unix_time(void);

#endif /* __NTP_CLIENT_H__ */
