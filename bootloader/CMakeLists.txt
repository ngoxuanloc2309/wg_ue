cmake_minimum_required(VERSION 3.20)
project(GatewayV2_Bootloader LANGUAGES C ASM)

add_executable(Bootloader)

# Stage-0 bootloader sources. Keep this target independent from HAL/USB and
# the OTA metadata library so the normal jump path cannot block on either.
target_sources(Bootloader PRIVATE
    Core/Src/main.c
    Core/Src/system_stm32h5xx.c
    startup_stm32h563xx.s
)

target_include_directories(Bootloader PRIVATE
    Core/Inc
    Inc
    Drivers/CMSIS/Device/ST/STM32H5xx/Include
    Drivers/CMSIS/Include
    ../libs/bootloader
)

target_compile_definitions(Bootloader PRIVATE STM32H563xx)

target_compile_options(Bootloader PRIVATE -Os)

# The bootloader toolchain file supplies STM32H563xx_FLASH.ld globally.
# Keep a single linker-script argument to avoid GNU ld rejecting duplicates.

add_custom_command(TARGET Bootloader POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary
            $<TARGET_FILE:Bootloader>
            ${CMAKE_BINARY_DIR}/Bootloader.bin
    COMMENT "Generating Bootloader.bin"
)
