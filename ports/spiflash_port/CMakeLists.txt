add_library(ports_spiflash_port STATIC)
target_sources(ports_spiflash_port PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/spiflash.c
)
target_include_directories(ports_spiflash_port PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ports_spiflash_port PUBLIC stm32cubemx drivers_spiflash)
