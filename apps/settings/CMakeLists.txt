add_library(apps_settings STATIC)
target_sources(apps_settings PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/config_manager.c
    ${CMAKE_CURRENT_SOURCE_DIR}/config_apply_manager.c
)
target_include_directories(apps_settings PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(apps_settings PUBLIC stm32cubemx utils_cjson)
