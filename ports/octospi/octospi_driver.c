/**
 * @file octospi_driver.c
 * @brief OctoSPI Driver - MX25LM51245 Flash
 */
#include "octospi_driver.h"

int octospi_init(void) {
    return 0;
}

int octospi_read(uint32_t addr, void *buf, uint32_t len) {
    (void)addr; (void)buf; (void)len;
    return -1;
}

int octospi_write(uint32_t addr, const void *buf, uint32_t len) {
    (void)addr; (void)buf; (void)len;
    return -1;
}

int octospi_erase(uint32_t addr, uint32_t len) {
    (void)addr; (void)len;
    return -1;
}
