# CaDS Zero - storage module (littlefs on internal flash, bank 2).
#
# Self-contained like modules/toolbox: usable both as add_subdirectory(modules/storage)
# from the top level and configured standalone (cmake -S modules/storage -B build),
# which is what lets the host tests run without the rest of the firmware.
#
# Target selection mirrors the top-level CMakeLists.txt exactly: a "Generic"
# system name means the ARM cross toolchain file is active, so this is a board
# build and the STM32F4 register-level driver is compiled; anything else is
# the host, and the RAM-backed emulation is compiled instead. Duplicated here
# rather than shared because this module has to configure standalone.

cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(cads-zero-storage C)
    set(CMAKE_C_STANDARD 11)
    set(CMAKE_C_STANDARD_REQUIRED ON)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

if(NOT DEFINED CADS_ROOT)
    get_filename_component(CADS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
endif()

if(NOT TARGET cads_flags)
    add_library(cads_flags INTERFACE)
    target_compile_options(cads_flags INTERFACE -Wall -Wextra -Wshadow -Wundef -Wno-unused-parameter)
endif()

if(NOT EXISTS ${CADS_ROOT}/lib/littlefs/lfs.c)
    message(FATAL_ERROR "lib/littlefs is empty. Run: git submodule update --init lib/littlefs")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Generic")
    set(CADS_STORAGE_FLASH_SOURCE src/cads_flash_stm32f4.c)
    set(CADS_STORAGE_TARGET_INCLUDES
        ${CADS_ROOT}/targets/itsboard
        ${CADS_ROOT}/targets/itsboard/hal
        ${CADS_ROOT}/core
        ${CADS_ROOT}/lib/CMSIS_6/CMSIS/Core/Include
        ${CADS_ROOT}/lib/cmsis_device_f4/Include
    )
    set(CADS_STORAGE_TARGET_DEFINES STM32F429xx CADS_TARGET_ITSBOARD=1)
else()
    set(CADS_STORAGE_FLASH_SOURCE src/cads_flash_host.c)
    set(CADS_STORAGE_TARGET_INCLUDES "")
    set(CADS_STORAGE_TARGET_DEFINES "")
endif()

add_library(cads_storage STATIC
    src/cads_storage.c
    src/cads_kv.c
    ${CADS_STORAGE_FLASH_SOURCE}
    ${CADS_ROOT}/lib/littlefs/lfs.c
    ${CADS_ROOT}/lib/littlefs/lfs_util.c
)

target_include_directories(cads_storage
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CADS_ROOT}/lib/littlefs
            ${CADS_STORAGE_TARGET_INCLUDES})

# LFS_NAME_MAX must be set before lfs.h is parsed by anything, hence a
# compile definition rather than a runtime value - see the _Static_assert in
# src/cads_storage.c that keeps it honest against CADS_STORAGE_NAME_MAX.
# LFS_NO_MALLOC makes any code path that would need the heap a build failure
# instead of a silent malloc() on a device that configures none; the
# LFS_NO_{DEBUG,WARN,ERROR} trio keeps littlefs's optional printf tracing out
# of a firmware that links no formatted output anywhere else (see
# modules/toolbox/README.md).
target_compile_definitions(cads_storage PRIVATE
    LFS_NAME_MAX=63
    LFS_NO_MALLOC
    LFS_NO_DEBUG
    LFS_NO_WARN
    LFS_NO_ERROR
    ${CADS_STORAGE_TARGET_DEFINES}
)

target_link_libraries(cads_storage PRIVATE cads_flags)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    enable_testing()
    add_subdirectory(tests)
endif()
