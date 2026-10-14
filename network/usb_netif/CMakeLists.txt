cmake_minimum_required(VERSION 3.22)

enable_language(C CXX ASM)

add_library(network_usb_netif STATIC)

if(GW_USB_CDC_ENABLE)
    # CDC-only USB device: no USB network function, so usb_netif.c (lwIP netif,
    # DHCP/DNS server, RNDIS/ECM callbacks) is replaced by a stub that keeps the
    # two public functions alive.
    set(USB_NET_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/usb_netif_stub.c)
else()
    file(GLOB USB_NET_SOURCE "usb_netif.c")
endif()

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