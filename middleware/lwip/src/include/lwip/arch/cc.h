/**
 * @file cc.h
 * @brief lwIP ARM Cortex-M platform specific definitions
 */
#ifndef __LWIP_CC_H__
#define __LWIP_CC_H__

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Define platform endianness */
#define LWIP_PLATFORM_BIGENDIAN 0

/* Define generic types */
typedef uint8_t  u8_t;
typedef int8_t   s8_t;
typedef uint16_t u16_t;
typedef int16_t  s16_t;
typedef uint32_t u32_t;
typedef int32_t  s32_t;
typedef uintptr_t mem_ptr_t;

/* Define (non)alignment checks */
#define LWIP_PACK_ALIGNMENT 1

/* Debugging macros */
// #define LWIP_PLATFORM_DIAG(x) printf x

#define LWIP_PLATFORM_ASSERT(x) do { printf("Assertion \"%s\" failed at line %d in %s\n", x, __LINE__, __FILE__); } while(0)

/* Critical section - stub for now */
#define SYS_ARCH_DECL_PROTECT(x)
#define SYS_ARCH_PROTECT(x)
#define SYS_ARCH_UNPROTECT(x)

/* Timing - stub */
#define LWIP_ARCH_GET_US()    0
#define LWIP_ARCH_GET_MS()   0

#endif /* __LWIP_CC_H__ */
