add_library(drivers_octospi STATIC)
target_sources(drivers_octospi PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/octospi_driver.c
)
target_include_directories(drivers_octospi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(drivers_octospi PUBLIC stm32cubemx)
