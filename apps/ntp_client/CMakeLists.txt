add_library(apps_ntp_client STATIC ntp_client.c)
target_include_directories(apps_ntp_client PUBLIC .)
target_link_libraries(apps_ntp_client PUBLIC 
    middleware_lwip 
    apps_settings 
    logger 
    RTOS2
)
