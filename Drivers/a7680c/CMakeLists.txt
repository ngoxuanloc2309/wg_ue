# Add a7680c to the build
add_library(drivers_a7680c STATIC a7680c.c)

target_include_directories(drivers_a7680c
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(drivers_a7680c
    PUBLIC STM32_Drivers logger RTOS2
)
