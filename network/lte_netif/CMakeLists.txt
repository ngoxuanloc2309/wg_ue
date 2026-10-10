add_library(network_lte_netif STATIC)
target_sources(network_lte_netif PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/lte_driver.c
)
target_include_directories(network_lte_netif PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(network_lte_netif
    PUBLIC middleware_lwip drivers_uart drivers_a7680c STM32_Drivers logger RTOS2
)
