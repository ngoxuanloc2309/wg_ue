# ports/splc_port - glue between GatewayV2 and the SimplePLC library.
#
# SimplePLC is embedded like lwIP (docs: libs/simple_plc/simple_plc/docs/handoff.md,
# decisions 13 and 22): the host supplies this port + splcopts.h, and nothing
# inside SimplePLC or inside the host drivers is edited.
#
# Target names: SimplePLC already owns "splc_port" (its Layer 3.5), so the host
# target is called ports_splc_port, following the ports_* convention here.

# --- SimplePLC in library mode -------------------------------------------
# Must be set before add_subdirectory(): the library reads them at configure time.
set(SPLC_AS_LIBRARY ON CACHE BOOL "SimplePLC as a library for this host" FORCE)
set(SPLC_OPTS_DIR ${CMAKE_CURRENT_SOURCE_DIR} CACHE PATH
    "Directory containing splcopts.h of this product" FORCE)

# SimplePLC finds the host's logger.h (SPLC_LOGGER_BUILTIN = 0) and FreeRTOS
# headers through the include directories/targets already set up at top level.
add_subdirectory(${CMAKE_SOURCE_DIR}/libs/simple_plc/simple_plc
                 ${CMAKE_CURRENT_BINARY_DIR}/simple_plc)

# --- Host port -----------------------------------------------------------
add_library(ports_splc_port STATIC)
target_sources(ports_splc_port PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/splc_port.c
)
# sx_os_*() for FreeRTOS (SX_OS_USE_FREERTOS = 1) are OBJECT files that must be
# part of the EXECUTABLE itself: inside a static library the linker would not
# pull them in, because modbus_link.c (in another archive, later on the link
# line) is the one that needs them (handoff, decision 22).
target_sources(${CMAKE_PROJECT_NAME} PRIVATE $<TARGET_OBJECTS:splc_platform_freertos>)
target_include_directories(ports_splc_port PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ports_splc_port PUBLIC
    splc            # SimplePLC (INTERFACE target of all its layers)
    stm32cubemx     # FreeRTOS headers
    tinyusb         # tusb.h: the host owns USB (SPLC_PORT_USB_CDC = 0)
    drivers_uart    # rs485_driver.h
    logger
)