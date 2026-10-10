add_library(ports_modbus_port STATIC)
target_sources(ports_modbus_port PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mmb_serial.c
)
target_include_directories(ports_modbus_port PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ports_modbus_port PUBLIC middleware_nanomodbus drivers_uart)
