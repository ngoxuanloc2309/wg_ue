/**
 * @file bitwise.h
 * @brief Bitwise operation helpers
 */
#ifndef __BITWISE_HELPERS_H__
#define __BITWISE_HELPERS_H__

#include <stdint.h>

#define BIT(n)         (1UL << (n))
#define BIT_SET(x, n)  ((x) |= BIT(n))
#define BIT_CLR(x, n)  ((x) &= ~BIT(n))
#define BIT_GET(x, n)  (((x) >> (n)) & 1)
#define BIT_TOGGLE(x, n) ((x) ^= BIT(n))

#define GENMASK(h, l)  (((~0UL) << (l)) & (~0UL >> (31 - (h))))

#endif /* __BITWISE_HELPERS_H__ */
