cmake_minimum_required(VERSION 3.20)

# Toolchain files must be in place before project() runs, so the target is
# selected by pointing CMAKE_TOOLCHAIN_FILE at cmake/arm-none-eabi-gcc.cmake.
# Without it we build the host simulator and the unit tests.

project(cads-zero C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CADS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
list(APPEND CMAKE_MODULE_PATH ${CADS_ROOT}/cmake)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

# SB121/SB122 on the Nucleo decide whether Arduino D11 lands on PA7 or PB5.
# See targets/itsboard/board.h and docs/HARDWARE.md.
set(CADS_SPI_MOSI_ON_PB5 0 CACHE STRING
    "1 after swapping SB121/SB122 so SPI1_MOSI uses PB5 and Ethernet keeps PA7")

if(CMAKE_SYSTEM_NAME STREQUAL "Generic")
    set(CADS_TARGET "itsboard")
else()
    set(CADS_TARGET "sim")
endif()
message(STATUS "CaDS Zero target: ${CADS_TARGET}")

# --- warnings ----------------------------------------------------------------
# Firmware that silently truncates an integer is firmware that fails in the
# field, so conversions are errors here rather than noise.
add_library(cads_flags INTERFACE)
target_compile_options(cads_flags INTERFACE
    -Wall
    -Wextra
    -Wshadow
    -Wundef
    -Wdouble-promotion
    -Wformat=2
    -Wno-unused-parameter
    $<$<CONFIG:Debug>:-Og>
    $<$<CONFIG:Debug>:-g3>
    $<$<CONFIG:Release>:-Os>
    $<$<CONFIG:Release>:-g>
    -ffunction-sections
    -fdata-sections
)

# --- portable layers ---------------------------------------------------------
add_library(cads_gui STATIC
    gui/canvas.c
)
target_include_directories(cads_gui PUBLIC
    ${CADS_ROOT}/gui
    ${CADS_ROOT}/core
)
target_link_libraries(cads_gui PUBLIC cads_flags)

add_library(cads_services STATIC
    services/input/cads_input.c
)
target_include_directories(cads_services PUBLIC ${CADS_ROOT}/services)
target_link_libraries(cads_services PUBLIC cads_gui cads_flags)

add_library(cads_apps STATIC
    apps/bringup/bringup.c
    apps/bringup/input_probe.c
    apps/bringup/explorer.c
)
target_include_directories(cads_apps PUBLIC ${CADS_ROOT}/apps)
target_link_libraries(cads_apps PUBLIC cads_services cads_gui cads_flags)

# --- target specific ---------------------------------------------------------
if(CADS_TARGET STREQUAL "itsboard")
    add_subdirectory(targets/itsboard)
    target_link_libraries(cads_gui PUBLIC cads_hal_itsboard)

    set(CADS_LINKER_SCRIPT ${CADS_ROOT}/targets/itsboard/linker/cads_itsboard.ld)

    add_executable(cads-zero.elf targets/itsboard/main.c)
    target_link_libraries(cads-zero.elf PRIVATE cads_apps cads_services cads_gui cads_hal_itsboard cads_flags)
    target_link_options(cads-zero.elf PRIVATE
        -T${CADS_LINKER_SCRIPT}
        -Wl,--gc-sections
        -Wl,-Map=${CMAKE_BINARY_DIR}/cads-zero.map
        -Wl,--print-memory-usage
        -Wl,--no-warn-rwx-segments
        -nostartfiles
    )
    set_target_properties(cads-zero.elf PROPERTIES LINK_DEPENDS ${CADS_LINKER_SCRIPT})

    add_custom_command(TARGET cads-zero.elf POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:cads-zero.elf> ${CMAKE_BINARY_DIR}/cads-zero.bin
        COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:cads-zero.elf> ${CMAKE_BINARY_DIR}/cads-zero.hex
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:cads-zero.elf>
        COMMENT "Producing cads-zero.bin / .hex and reporting size"
    )
else()
    add_subdirectory(targets/sim)
    enable_testing()
    add_subdirectory(tests)
endif()
