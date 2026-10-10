add_library(apps_shell STATIC shell_serial.c cli_shell_command.c)
target_include_directories(apps_shell PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/network/wifi_netif
    ${CMAKE_SOURCE_DIR}/network/eth_netif
    ${CMAKE_SOURCE_DIR}/ports/st67w611_port
)
target_link_libraries(apps_shell PUBLIC
    stm32cubemx
    components_cli_shell
    logger
    st67w611
    st67w611_port
    apps_mqtt_client
)
