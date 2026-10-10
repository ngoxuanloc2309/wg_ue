add_library(apps_http_server STATIC)
target_sources(apps_http_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/http_server.c
)
target_include_directories(apps_http_server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(apps_http_server PUBLIC stm32cubemx middleware_mongoose ports_mongoose_port middleware_littlefs apps_ota apps_ntp_client apps_telemetry_store)
