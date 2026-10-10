add_library(ports_flash_port STATIC)
target_sources(ports_flash_port PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/flash_port.c
)
target_include_directories(ports_flash_port PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ports_flash_port PUBLIC stm32cubemx libs_bootloader)
