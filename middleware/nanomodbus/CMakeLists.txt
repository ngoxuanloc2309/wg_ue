add_library(middleware_nanomodbus STATIC)
target_sources(middleware_nanomodbus PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/nanomodbus.c
)
target_include_directories(middleware_nanomodbus PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
