add_library(drivers_uart STATIC)
target_sources(drivers_uart PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/rs485_driver.c
)
target_include_directories(drivers_uart PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(drivers_uart PUBLIC stm32cubemx)
