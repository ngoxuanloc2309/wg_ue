#ifndef BOOT_SHA256_H
#define BOOT_SHA256_H

#include <stdint.h>

typedef struct {
    uint32_t state[8];
    uint64_t bit_count;
    uint8_t buffer[64];
    uint32_t buffer_len;
} boot_sha256_ctx_t;

void boot_sha256_init(boot_sha256_ctx_t *ctx);
void boot_sha256_update(boot_sha256_ctx_t *ctx, const uint8_t *data, uint32_t len);
void boot_sha256_final(boot_sha256_ctx_t *ctx, uint8_t digest[32]);

#endif
