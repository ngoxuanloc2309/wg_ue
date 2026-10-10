#ifndef BOOT_DEBUG_H
#define BOOT_DEBUG_H

#include "logger.h"

#define BOOT_DEBUG(...) LOGD("BOOT", ##__VA_ARGS__)
#define BOOT_ERROR(...) LOGE("BOOT", ##__VA_ARGS__)
#define BOOT_INFO(...) LOGI("BOOT", ##__VA_ARGS__)
#define BOOT_WARN(...) LOGW("BOOT", ##__VA_ARGS__)

#endif // BOOT_DEBUG_H