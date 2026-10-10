#ifndef A7680C_H
#define A7680C_H

#include <stdint.h>

/**
 * @brief Initialize A7680C driver and UART DMA
 */
void a7680c_init(void);

/**
 * @brief Power on the modem via GPIO
 */
void a7680c_power_on(void);

/**
 * @brief Perform a hard reset via GPIO
 */
void a7680c_hard_reset(void);

/**
 * @brief Setup network APN
 * @param apn: APN string (e.g. "internet")
 * @return 0 on success, -1 on failure
 */
int a7680c_setup_network(const char *apn);

/**
 * @brief Wait for AT readiness
 * @return 0 registered, 1 pending, 2 rejected/not-ready, -1 AT/transport error
 */
int a7680c_wait_at_ready(void);

/**
 * @brief Wait for network registration
 * @return 0 registered, 1 pending, 2 rejected/not-ready, -1 AT/transport error
 */
int a7680c_wait_network_registration(void);

/**
 * @brief Dial PPP
 * @return 0 on success, -1 on failure
 */
int a7680c_dial_ppp(void);

/**
 * @brief Transmit data (raw PPP or AT commands)
 * @param data: Data to transmit
 * @param len: Length of data
 * @return Number of bytes transmitted, or -1 on error
 */
int a7680c_transmit(const uint8_t *data, uint16_t len);

/**
 * @brief Read received data from the internal ring buffer
 * @param buf: Buffer to read into
 * @param max_len: Maximum length to read
 * @return Number of bytes read
 */
int a7680c_receive(uint8_t *buf, uint16_t max_len);

#endif /* A7680C_H */
