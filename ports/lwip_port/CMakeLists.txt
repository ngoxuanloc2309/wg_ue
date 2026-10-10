add_library(ports_lwip_port STATIC)
target_sources(ports_lwip_port PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/ethernetif.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../libs/stm32-dp83848/dp83848.c
)
target_include_directories(ports_lwip_port PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../libs/stm32-dp83848
    ${CMAKE_SOURCE_DIR}/network/eth_netif
)
target_link_libraries(ports_lwip_port PUBLIC stm32cubemx middleware_lwip)
