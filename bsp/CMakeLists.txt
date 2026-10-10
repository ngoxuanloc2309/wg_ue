add_library(bsp STATIC)
target_sources(bsp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/bsp.c
)
target_include_directories(bsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bsp PUBLIC stm32cubemx logger)
