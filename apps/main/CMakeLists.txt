add_library(apps_main STATIC)
target_sources(apps_main PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/gateway.c
)
target_include_directories(apps_main PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(apps_main PUBLIC stm32cubemx logger apps_ntp_client apps_mqtt_client)
