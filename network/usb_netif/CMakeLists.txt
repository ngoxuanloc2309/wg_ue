cmake_minimum_required(VERSION 3.22)

enable_language(C CXX ASM)

add_library(network_usb_netif STATIC)

file(GLOB USB_NET_SOURCE "usb_netif.c")

target_sources(network_usb_netif PRIVATE ${USB_NET_SOURCE})

target_include_directories(network_usb_netif PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../libs/tinyusb/lib/networking
)

target_link_libraries(network_usb_netif PUBLIC
    tinyusb
    stm32cubemx
    network_network_manager
)