# Compile-only boundary targets. These deliberately do not link into firmware.
add_library(st67w611_api_boundary_test OBJECT st67w611_api_compile.c)
target_include_directories(st67w611_api_boundary_test PRIVATE
    ${CMAKE_SOURCE_DIR}/libs/st67w611/include
)

add_library(st67w611_port_contract_test OBJECT st67w611_port_mock.c)
target_include_directories(st67w611_port_contract_test PRIVATE
    ${CMAKE_SOURCE_DIR}/libs/st67w611/include
)
