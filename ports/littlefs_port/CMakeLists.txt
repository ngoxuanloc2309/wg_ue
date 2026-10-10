add_library(ports_littlefs_port STATIC)
target_sources(ports_littlefs_port PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/block_device.c
)
target_include_directories(ports_littlefs_port PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ports_littlefs_port PUBLIC stm32cubemx middleware_littlefs)
