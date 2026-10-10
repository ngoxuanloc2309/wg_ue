add_library(utils_cqueue STATIC)

file(GLOB_RECURSE CQUEUE_SOURCES "*.c")

target_sources(utils_cqueue PRIVATE ${CQUEUE_SOURCES})

target_include_directories(utils_cqueue PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(utils_cqueue PUBLIC stm32cubemx)
