add_library(middleware_mongoose STATIC)

# Mongoose core sources only (exclude duplicate glue from ports/)
set(MONGOOSE_CORE "mongoose.c" "mongoose_fs.c")

target_sources(middleware_mongoose PRIVATE ${MONGOOSE_CORE})

target_include_directories(middleware_mongoose PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(middleware_mongoose PUBLIC stm32cubemx)

# Báo cho mongoose sử dụng cấu hình từ mongoose_config.h
target_compile_definitions(middleware_mongoose PUBLIC MG_ARCH=MG_ARCH_CUSTOM)
