add_library(apps_mqtt_client STATIC
    mqtt_client.c
)

target_include_directories(apps_mqtt_client PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/middleware/lwip/src/include
    ${CMAKE_SOURCE_DIR}/apps/config
    ${CMAKE_SOURCE_DIR}/apps/logger
)

target_link_libraries(apps_mqtt_client PUBLIC
    stm32cubemx
    middleware_lwip
)
