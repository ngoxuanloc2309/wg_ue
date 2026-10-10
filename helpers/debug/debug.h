/**
 * @file debug.h
 * @brief Debug helpers
 */
#ifndef __DEBUG_HELPERS_H__
#define __DEBUG_HELPERS_H__

#include <stdio.h>

#define DBG_PRINT(fmt, ...)  printf("[DBG] " fmt "\r\n", ##__VA_ARGS__)
#define DBG_HEX(data, len)  do { for (int i = 0; i < (len); i++) printf("%02X ", (data)[i]); printf("\r\n"); } while(0)

#endif /* __DEBUG_HELPERS_H__ */
