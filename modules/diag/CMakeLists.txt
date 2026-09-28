# CaDS Zero - diag module: the crash forensics ring buffer.
#
# Fully portable - cads_forensic.c only calls cads_hal_ticks_ms() and uses
# the CADS_CCM_SECTION placement attribute, both already defined identically
# for board, sim and the unit-test fake HAL in core/cads_hal.h. No target
# split needed, unlike modules/net or modules/storage.

if(NOT DEFINED CADS_ROOT)
    get_filename_component(CADS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
endif()

if(NOT TARGET cads_flags)
    add_library(cads_flags INTERFACE)
    target_compile_options(cads_flags INTERFACE -Wall -Wextra -Wshadow -Wundef -Wno-unused-parameter)
endif()

add_library(cads_diag STATIC src/cads_forensic.c src/cads_bootguard.c)

target_include_directories(cads_diag
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CADS_ROOT}/core)

target_link_libraries(cads_diag PRIVATE cads_flags)

# CADS_CCM_SECTION (core/cads_hal.h) only places the ring in CCM when
# CADS_TARGET_ITSBOARD is defined for THIS translation unit - without this,
# cads_diag silently built as a target-neutral module never received it, and
# the forensic ring landed in plain RAM instead of CCM: harmless-looking on
# its own, but it ate directly into the 48K heap floor
# targets/itsboard/linker/cads_itsboard.ld's own ASSERT enforces, and that
# ASSERT is exactly what caught it. CCM has no such floor to threaten.
if(DEFINED CADS_TARGET AND CADS_TARGET STREQUAL "itsboard")
    target_compile_definitions(cads_diag PRIVATE CADS_TARGET_ITSBOARD=1)
endif()
