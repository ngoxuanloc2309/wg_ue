/**
 * @file buffer.h
 * @brief Buffer manipulation helpers
 */
#ifndef __BUFFER_HELPERS_H__
#define __BUFFER_HELPERS_H__

#include <stdint.h>

#define BUFFER_SIZE(x)  (sizeof(x) / sizeof((x)[0]))

void buffer_clear(uint8_t *buf, uint32_t len);
void buffer_copy(uint8_t *dst, const uint8_t *src, uint32_t len);
uint32_t buffer_find(uint8_t *haystack, uint32_t hlen, const uint8_t *needle, uint32_t nlen);

#endif /* __BUFFER_HELPERS_H__ */
