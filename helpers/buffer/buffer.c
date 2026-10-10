/**
 * @file buffer.c
 * @brief Buffer manipulation helpers implementation
 */
#include "buffer.h"
#include <string.h>

void buffer_clear(uint8_t *buf, uint32_t len) {
    memset(buf, 0, len);
}

void buffer_copy(uint8_t *dst, const uint8_t *src, uint32_t len) {
    memcpy(dst, src, len);
}

uint32_t buffer_find(uint8_t *haystack, uint32_t hlen, const uint8_t *needle, uint32_t nlen) {
    if (nlen == 0 || hlen < nlen) return hlen;
    for (uint32_t i = 0; i <= hlen - nlen; i++) {
        if (memcmp(haystack + i, needle, nlen) == 0) return i;
    }
    return hlen;
}
