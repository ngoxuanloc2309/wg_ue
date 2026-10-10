add_library(middleware_lwip STATIC)

# Core lwIP sources - only essential modules
file(GLOB LWIP_CORE "src/core/*.c")
file(GLOB LWIP_CORE_IPV4 "src/core/ipv4/*.c")
file(GLOB LWIP_CORE_IPV6 "src/core/ipv6/*.c")
file(GLOB LWIP_API "src/api/*.c")
# Only include essential netif files (exclude PPP)
file(GLOB LWIP_NETIF_ETH "src/netif/ethernet.c")
# PPP
file(GLOB LWIP_NETIF_PPP "src/netif/ppp/*.c")
file(GLOB LWIP_NETIF_PPP_POLARSSL "src/netif/ppp/polarssl/*.c")
# MQTT
file(GLOB LWIP_APPS_MQTT "src/apps/mqtt/*.c")
# SNTP
file(GLOB LWIP_APPS_SNTP "src/apps/sntp/*.c")
# altcp TLS
file(GLOB LWIP_ALTCP_TLS "src/apps/altcp_tls/*.c")
# OS port
file(GLOB LWIP_OS "system/OS/sys_arch.c")

target_sources(middleware_lwip PRIVATE
    ${LWIP_CORE}
    ${LWIP_CORE_IPV4}
    ${LWIP_CORE_IPV6}
    ${LWIP_API}
    ${LWIP_NETIF_ETH}
    ${LWIP_NETIF_PPP}
    ${LWIP_NETIF_PPP_POLARSSL}
    ${LWIP_APPS_MQTT}
    ${LWIP_APPS_SNTP}
    ${LWIP_ALTCP_TLS}
    ${LWIP_OS}
)

target_include_directories(middleware_lwip PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/include/lwip
    ${CMAKE_CURRENT_SOURCE_DIR}/src/include/lwip/ipv4
    ${CMAKE_CURRENT_SOURCE_DIR}/src/include/lwip/ipv6
    ${CMAKE_CURRENT_SOURCE_DIR}/src/include/lwip/arch
    ${CMAKE_CURRENT_SOURCE_DIR}/system
    ${CMAKE_CURRENT_SOURCE_DIR}/src/apps/mqtt
    ${CMAKE_CURRENT_SOURCE_DIR}/src/apps/http
    ${CMAKE_CURRENT_SOURCE_DIR}/src/apps/sntp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/apps/altcp_tls
)

target_link_libraries(middleware_lwip PUBLIC stm32cubemx)

