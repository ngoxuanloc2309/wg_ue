add_library(apps_data_broker STATIC
    data_broker.c
)

target_include_directories(apps_data_broker PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/apps/logger
    ${CMAKE_SOURCE_DIR}/apps/config
    ${CMAKE_SOURCE_DIR}/apps/modbus_master
    ${CMAKE_SOURCE_DIR}/components/cqueue
    ${CMAKE_SOURCE_DIR}/components/cJSON
    ${CMAKE_SOURCE_DIR}/apps/mqtt_client
)

target_link_libraries(apps_data_broker PUBLIC stm32cubemx apps_ntp_client apps_mqtt_client)
