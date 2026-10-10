add_library(apps_modbus_tcp_server STATIC)
target_sources(apps_modbus_tcp_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/modbus_tcp_server.c
)
target_include_directories(apps_modbus_tcp_server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(apps_modbus_tcp_server PUBLIC 
    stm32cubemx 
    utils_cjson 
    middleware_nanomodbus
    middleware_lwip
    apps_settings
    apps_modbus_master
    logger
    RTOS2
    network_network_manager
)
