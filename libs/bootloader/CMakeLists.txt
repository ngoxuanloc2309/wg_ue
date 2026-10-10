add_library(libs_bootloader STATIC)
target_sources(libs_bootloader PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/bootloader.c
    ${CMAKE_CURRENT_SOURCE_DIR}/boot_flash.c
    ${CMAKE_CURRENT_SOURCE_DIR}/logger.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sha256.c
)
target_include_directories(libs_bootloader PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(libs_bootloader PUBLIC stm32cubemx)
