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

# littlefs is vendored, not ours - its warnings are not ours to fix, the same
# reasoning tests/CMakeLists.txt already applies to lib/Unity. Built as its
# own library rather than folded into cads_storage's sources so it never sees
# cads_flags' -Wshadow (which its own internal `int err` reuse trips on
# repeatedly - harmless, upstream's code, not worth carrying a diff for).
#
# LFS_NAME_MAX must be set before lfs.h is parsed by anything, hence a public
# compile definition rather than a runtime value - see the _Static_assert in
# src/cads_storage.c that keeps it honest against CADS_STORAGE_NAME_MAX.
# LFS_NO_MALLOC makes any code path that would need the heap a build failure
# instead of a silent malloc() on a device that configures none; the
# LFS_NO_{DEBUG,WARN,ERROR} trio keeps littlefs's optional printf tracing out
# of a firmware that links no formatted output anywhere else (see
# modules/toolbox/README.md).
#
# LFS_NO_ASSERT belongs to the same reasoning, found the hard way: without it
# lfs_util.h's default LFS_ASSERT(test) expands to the real assert(test),
# and newlib's assert() pulls in the fprintf/stdio machinery to report a
# failure - which pulls in _sbrk to grow a heap for its buffers, which is
# exactly the symbol this firmware's linker script leaves undefined on
# purpose (no heap, ever). The board build only fails to *link* over this,
# never to compile, so it was invisible until the ARM target was actually
# built - the host build links a real libc with a real heap and never
# noticed. A custom LFS_ASSERT that calls cads_hal_panic() instead of
# assert() would keep the runtime check while staying heap-free; left as a
# future improvement rather than done here, since core/cads_hal.h is not
# currently a dependency of this module and wiring one in for this alone
# deserves its own review pass, not a rushed addition while chasing a linker
# error.
add_library(cads_littlefs STATIC
    ${CADS_ROOT}/lib/littlefs/lfs.c
    ${CADS_ROOT}/lib/littlefs/lfs_util.c
)
target_include_directories(cads_littlefs PUBLIC ${CADS_ROOT}/lib/littlefs)
target_compile_definitions(cads_littlefs PUBLIC
    LFS_NAME_MAX=63
    LFS_NO_MALLOC
    LFS_NO_ASSERT
    LFS_NO_DEBUG
    LFS_NO_WARN
    LFS_NO_ERROR
)

add_library(cads_storage STATIC
    src/cads_storage.c
    src/cads_kv.c
    ${CADS_STORAGE_FLASH_SOURCE}
)

target_include_directories(cads_storage
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CADS_STORAGE_TARGET_INCLUDES})

target_compile_definitions(cads_storage PRIVATE ${CADS_STORAGE_TARGET_DEFINES})

target_link_libraries(cads_storage PRIVATE cads_flags PUBLIC cads_littlefs)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    enable_testing()
    add_subdirectory(tests)
endif()
