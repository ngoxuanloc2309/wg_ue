add_library(st67w611_port STATIC st67w611_port.c)
target_include_directories(st67w611_port PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/libs/st67w611/include
)
target_include_directories(st67w611_port PRIVATE
    ${CMAKE_SOURCE_DIR}/libs/st67w611/inc
    ${CMAKE_SOURCE_DIR}/libs/st67w611/port/include
)
target_link_libraries(st67w611_port PUBLIC stm32cubemx logger)
