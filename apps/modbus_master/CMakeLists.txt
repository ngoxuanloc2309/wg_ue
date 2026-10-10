add_library(apps_modbus_master STATIC)
target_sources(apps_modbus_master PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/modbus_master.c
    ${CMAKE_CURRENT_SOURCE_DIR}/modbus_parser.c
)
target_include_directories(apps_modbus_master PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(apps_modbus_master PUBLIC stm32cubemx middleware_nanomodbus ports_modbus_port)
