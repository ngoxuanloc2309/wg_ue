/**
 * @file mongoose_lfs.h
 * @brief LittleFS adapter for Mongoose HTTP server
 */
#ifndef __MONGOOSE_LFS_H__
#define __MONGOOSE_LFS_H__

#include "mongoose.h"

/**
 * @brief Get LittleFS mg_fs pointer for serving static files
 * @return Pointer to mg_fs structure for use with mg_http_serve_opts
 */
struct mg_fs* mongoose_lfs_get_fs(void);

#endif /* __MONGOOSE_LFS_H__ */
