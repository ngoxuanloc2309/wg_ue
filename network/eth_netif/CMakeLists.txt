add_library(network_eth_netif STATIC)
target_sources(network_eth_netif PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/eth_driver.c
)
target_include_directories(network_eth_netif PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/ports/lwip_port
)
target_link_libraries(network_eth_netif PUBLIC stm32cubemx middleware_lwip ports_lwip_port)
