cmake_minimum_required(VERSION 3.22)

#
# GatewayV2 - STM32H563 Industrial IoT Gateway
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME GatewayV2)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

option(ST67_SPI_DIAG_POLLING
       "Use blocking HAL SPI instead of DMA for ST67 transport diagnostics"
       OFF)

option(ST67_SPI_LOOPBACK
       "Build standalone STM32 SPI MOSI-to-MISO loopback diagnostic"
       OFF)

option(GW_USB_NETIF_ENABLE
       "Enable USB network configuration interface"
       OFF)

option(GW_MDNS_ENABLE
       "Enable mDNS Web Config discovery"
       OFF)

# Define macros for ST67W611 library
add_compile_definitions(USE_ST67W611=1)
set(ST67W611_ARCH T02 CACHE STRING "ST67 architecture (T01 or T02)")
set_property(CACHE ST67W611_ARCH PROPERTY STRINGS T01 T02)
if(ST67W611_ARCH STREQUAL "T01")
    set(ST67_ARCH 1)
elseif(ST67W611_ARCH STREQUAL "T02")
    set(ST67_ARCH 2)
else()
    message(FATAL_ERROR "ST67W611_ARCH must be T01 or T02")
endif()
add_compile_definitions(ST67_ARCH=${ST67_ARCH})
# Keep vendor RX/SPI tasks below the Gateway shell/monitor band while bringing
# up the port. This prevents a malformed no-data path from starving the app.
# Diagnostic bring-up: keep the SPI engine below the application task band so
# a malformed RDY/event loop cannot starve the shell and monitor.
add_compile_definitions(W61_MDM_RX_TASK_PRIO=8)
add_compile_definitions(SPI_THREAD_PRIO=7)
add_compile_definitions(LOG_THREAD_PRIO=6)
if(ST67_SPI_DIAG_POLLING)
    add_compile_definitions(ST67_SPI_DIAG_POLLING=1)
endif()
if(ST67_SPI_LOOPBACK)
    add_compile_definitions(ST67_SPI_LOOPBACK=1)
endif()
if(GW_USB_NETIF_ENABLE)
    add_compile_definitions(GW_USB_NETIF_ENABLE=1)
else()
    add_compile_definitions(GW_USB_NETIF_ENABLE=0)
endif()
if(GW_MDNS_ENABLE)
    add_compile_definitions(GW_MDNS_ENABLE=1)
else()
    add_compile_definitions(GW_MDNS_ENABLE=0)
endif()

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Export global config directories so all sub-modules can find them
include_directories(
    configs
    bsp
    Core/Inc
    ports
    ports/modbus_port
    ports/lwip_port
    ports/littlefs_port
    ports/mongoose_port
    ports/spiflash_port
    libs/st67w611/inc
    libs/st67w611/include
    libs/x-cube-st67w61/Middlewares/ST/ST67W6X_Network_Driver/Core/Inc
    drivers/uart
    ports/octospi
    drivers
    drivers/spiflash
    network
    network/network_manager
    network/eth_netif
    network/wifi_netif
    network/lte_netif
    network/usb_netif
    apps
    apps/main
    apps/serial_tcp_server
    apps/modbus_master
    apps/data_broker
    apps/telemetry_store
    apps/http_server
    apps/ota
    apps/settings
    apps/modbus_tcp_server
    apps/shell
    libs/logger
    components
    components/cli_shell
    utils/cqueue
    utils/file_io
    middleware
    middleware/lwip
    middleware/lwip/src/include
    middleware/lwip/Target
    middleware/lwip/src/apps/mqtt
    middleware/lwip/src/apps/http
    middleware/lwip/src/apps/sntp
    middleware/lwip/src/apps/altcp_tls
    middleware/nanomodbus
    middleware/mongoose
    utils
    libs/littlefs
    libs/cJSON
    libs/mbedtls/include
    libs/x-cube-st67w61/Middlewares/ST/ST67W6X_Network_Driver/Api
    libs/x-cube-st67w61/Middlewares/ST/ST67W6X_Network_Driver/Core
    libs/x-cube-st67w61/Middlewares/ST/ST67W6X_Network_Driver/Driver/W61_bus
    libs/x-cube-st67w61/Middlewares/ST/ST67W6X_Network_Driver/Driver/W61_at
    libs/x-cube-st67w61/Middlewares/ST/ST67W6X_Network_Driver/Utils/Logging
    libs/x-cube-st67w61/Middlewares/ST/ST67W6X_Network_Driver/Utils/Misc
)

# Add custom project modules
# Drivers
add_subdirectory(drivers/spiflash)
# Components
add_subdirectory(utils/cqueue)
add_subdirectory(apps/mqtt_client)
add_subdirectory(components/cli_shell)
add_subdirectory(utils/file_io)
# Middleware
add_subdirectory(middleware/lwip)
add_subdirectory(middleware/nanomodbus)
add_subdirectory(middleware/mongoose)
# Utils
# Libs
add_subdirectory(libs)
# Ports
add_subdirectory(ports/modbus_port)
add_subdirectory(ports/lwip_port)
add_subdirectory(ports/littlefs_port)
add_subdirectory(ports/mongoose_port)
add_subdirectory(ports/spiflash_port)
add_subdirectory(libs/st67w611)
add_subdirectory(libs/st67w611/adapters/lwip)
add_subdirectory(tests)
add_subdirectory(ports/st67w611_port)
add_subdirectory(ports/flash_port)
add_subdirectory(drivers/uart)
add_subdirectory(drivers/a7680c)
add_subdirectory(ports/octospi)
add_subdirectory(bootloader)
# Network
add_subdirectory(network/network_manager)
add_subdirectory(network/eth_netif)
add_subdirectory(network/wifi_netif)
add_subdirectory(network/lte_netif)
add_subdirectory(network/usb_netif)
# BSP
add_subdirectory(bsp)
# Apps
add_subdirectory(apps/main)
add_subdirectory(apps/serial_tcp_server)
add_subdirectory(apps/modbus_master)
add_subdirectory(apps/data_broker)
add_subdirectory(apps/http_server)
add_subdirectory(apps/telemetry_store)
add_subdirectory(apps/ota)
add_subdirectory(apps/settings)
add_subdirectory(apps/modbus_tcp_server)
add_subdirectory(apps/ntp_client)
add_subdirectory(apps/shell)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
    # Apps
    apps_main
    apps_serial_tcp_server
    apps_modbus_master
    apps_data_broker
    apps_telemetry_store
    apps_http_server
    apps_ota
    apps_settings
    apps_modbus_tcp_server
    apps_ntp_client
    apps_shell
    # Network
    network_network_manager
    network_eth_netif
    network_wifi_netif
    network_lte_netif
    network_usb_netif
    # BSP
    bsp
    # Ports
    ports_modbus_port
    ports_lwip_port
    ports_littlefs_port
    ports_mongoose_port
    ports_spiflash_port
    st67w611
    st67w611_port
    # Middleware & Components
    apps_mqtt_client
    components_cli_shell
    logger
    utils_cqueue
    utils_file_io
    middleware_lwip
    middleware_mbedtls
    middleware_nanomodbus
    middleware_littlefs
    middleware_mongoose
    # Drivers
    drivers_spiflash
    drivers_uart
    drivers_a7680c
    drivers_octospi
    tinyusb
    # Utils
    utils_cjson
)

add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex
            $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
            ${CMAKE_PROJECT_NAME}.hex

    COMMAND ${CMAKE_OBJCOPY} -O binary
            $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
            ${CMAKE_PROJECT_NAME}.bin

    COMMENT "Generating HEX and BIN files"
)
