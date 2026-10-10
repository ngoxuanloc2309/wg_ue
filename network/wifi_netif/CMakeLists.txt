add_library(network_wifi_netif STATIC)
target_sources(network_wifi_netif PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/wifi_driver.c
)
if(ST67_ARCH EQUAL 2)
    target_sources(network_wifi_netif PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/wifi_netif.c
    )
endif()
target_include_directories(network_wifi_netif PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Core/Inc
    ${CMAKE_SOURCE_DIR}/libs/st67w611/adapters/lwip/include
    ${CMAKE_SOURCE_DIR}/ports/st67w611_port
)
target_include_directories(network_wifi_netif PRIVATE
    ${CMAKE_SOURCE_DIR}/libs/st67w611/adapters/lwip/internal
)
target_link_libraries(network_wifi_netif PUBLIC
    stm32cubemx
    logger
    st67w611
    st67w611_port
)
if(ST67_ARCH EQUAL 2)
    target_link_libraries(network_wifi_netif PUBLIC middleware_lwip st67w611_lwip)
endif()
