add_library(drivers_spiflash STATIC)

file(GLOB SPIFLASH_SOURCES "*.c")

target_sources(drivers_spiflash PRIVATE ${SPIFLASH_SOURCES})

target_include_directories(drivers_spiflash PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(drivers_spiflash PUBLIC stm32cubemx logger)

