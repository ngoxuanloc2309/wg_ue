#pragma once

// See https://mongoose.ws/documentation/#build-options
#define MG_ARCH MG_ARCH_FREERTOS
#define MG_OTA MG_OTA_CUSTOM
#define MG_TLS MG_TLS_BUILTIN
#define MG_IO_SIZE 2048
#define MG_MAX_RECV_SIZE (32UL * 1024UL)

#define MG_ENABLE_LWIP 1
#define MG_ENABLE_SOCKET 1
#define MG_ENABLE_CUSTOM_MILLIS 0
#define MG_ENABLE_CUSTOM_RANDOM 0
#define MG_ENABLE_PACKED_FS 1
#define MG_ENABLE_DRIVER_STM32H 0
#define MG_ENABLE_POSIX_FS 0
#define MG_ENABLE_HTTP_WEBSOCKET 1
#define MG_ENABLE_HTTP_SERVER 1
