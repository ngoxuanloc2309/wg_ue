add_library(network_network_manager STATIC)
target_sources(network_network_manager PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/network_manager.c
)
target_include_directories(network_network_manager PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/network/wifi_netif
)
target_link_libraries(network_network_manager PUBLIC stm32cubemx middleware_lwip apps_settings)
