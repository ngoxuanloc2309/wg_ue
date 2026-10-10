add_library(apps_ota STATIC)
target_sources(apps_ota PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/ota.c
)
target_include_directories(apps_ota PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(apps_ota PUBLIC
    stm32cubemx
    libs_bootloader
    ports_flash_port
    logger
    RTOS2
    middleware_mbedtls
)
