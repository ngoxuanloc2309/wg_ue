/**
 * @file board.h
 * @brief Định nghĩa các thông số liên quan đến phần cứng (Hardware Abstraction)
 */

#ifndef BOARD_H
#define BOARD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief Khởi tạo các thành phần phần cứng cơ bản chưa được CubeMX cover
 */
void board_init(void);

/* ================== CẤU HÌNH SPI FLASH (W25Q128) ================== */
/* Chip W25Q128 có dung lượng 16MB (128Mbit) */
#define BSP_FLASH_SIZE          0x1000000  // 16 MB
#define BSP_FLASH_SECTOR_SIZE   0x1000     // 4 KB per sector
#define BSP_FLASH_SECTOR_COUNT  (BSP_FLASH_SIZE / BSP_FLASH_SECTOR_SIZE)

#ifdef __cplusplus
}
#endif

#endif /* BOARD_H */
