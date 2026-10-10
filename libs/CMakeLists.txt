cmake_minimum_required(VERSION 3.22)

# littlefs
add_library(middleware_littlefs STATIC)
file(GLOB LITTLEFS_SOURCES "littlefs/*.c")
target_sources(middleware_littlefs PRIVATE ${LITTLEFS_SOURCES})
target_include_directories(middleware_littlefs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/littlefs)
target_compile_definitions(middleware_littlefs PUBLIC LFS_THREADSAFE=1)
target_link_libraries(middleware_littlefs PUBLIC stm32cubemx)

# mbedtls
add_library(middleware_mbedtls STATIC)
file(GLOB MBEDTLS_SOURCES "mbedtls/library/*.c")
target_sources(middleware_mbedtls PRIVATE ${MBEDTLS_SOURCES})
target_include_directories(middleware_mbedtls PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/include
    ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/include/mbedtls
)
target_link_libraries(middleware_mbedtls PUBLIC stm32cubemx)
target_compile_definitions(middleware_mbedtls PUBLIC 
    MBEDTLS_NO_PLATFORM_ENTROPY
    MBEDTLS_CONFIG_FILE="mbedtls_config.h"
)

# cJSON
add_library(utils_cjson STATIC)
file(GLOB CJSON_SOURCES "cJSON/*.c")
target_sources(utils_cjson PRIVATE ${CJSON_SOURCES})
target_include_directories(utils_cjson PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/cJSON)
target_link_libraries(utils_cjson PUBLIC stm32cubemx)

# logger
add_subdirectory(logger)
target_link_libraries(logger PUBLIC stm32cubemx)

# tinyusb
add_subdirectory(tinyusb/src)
add_library(tinyusb STATIC)
tinyusb_target_add(tinyusb)
file(GLOB USB_NET_LIB_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/tinyusb/lib/networking/*.c)
target_sources(tinyusb PUBLIC 
        ${CMAKE_CURRENT_SOURCE_DIR}/../ports/usb_port/Target/usb_descriptors.c
        ${USB_NET_LIB_SOURCE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tinyusb/src/portable/st/stm32_fsdev/dcd_stm32_fsdev.c
        ${CMAKE_CURRENT_SOURCE_DIR}/tinyusb/src/portable/st/stm32_fsdev/fsdev_common.c
        ${CMAKE_CURRENT_SOURCE_DIR}/tinyusb/src/portable/st/stm32_fsdev/hcd_stm32_fsdev.c
        )
target_include_directories(tinyusb PUBLIC 
        ${CMAKE_CURRENT_SOURCE_DIR}/../ports/usb_port/Target
        ${CMAKE_CURRENT_SOURCE_DIR}/tinyusb/src/common 
        ${CMAKE_CURRENT_SOURCE_DIR}/tinyusb/src/device)
target_link_libraries(tinyusb PUBLIC 
    stm32cubemx
    middleware_lwip)
# bootloader library
add_subdirectory(bootloader)
