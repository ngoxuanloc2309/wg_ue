add_library(ports_mongoose_port STATIC)
target_sources(ports_mongoose_port PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mongoose_glue.c
    ${CMAKE_CURRENT_SOURCE_DIR}/mongoose_lfs.c
)
target_include_directories(ports_mongoose_port PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/ports/littlefs_port
)
target_link_libraries(ports_mongoose_port PUBLIC stm32cubemx middleware_mongoose middleware_littlefs)
