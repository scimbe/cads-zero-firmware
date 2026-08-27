# CaDS Zero - netx module (active tooling: forged-frame builders + raw L2
# I/O on the board, honest "no link" stubs on the simulator).
#
# The frame builders (src/cads_netx_frame.c) are portable - no HAL, no
# lwIP, no static state - and are built on BOTH targets so the host can
# golden-byte test them (tests/unit/test_netx_frame.c). The raw I/O half
# is the board/sim split modules/net and modules/storage already use:
# cads_netx_rawio_board.c talks to hal_eth_mac.h, cads_netx_rawio_sim.c
# is a no-op stub. See include/cads/netx/{frame,rawio}.h for the contract.

if(NOT DEFINED CADS_ROOT)
    get_filename_component(CADS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
endif()

if(NOT TARGET cads_flags)
    add_library(cads_flags INTERFACE)
    target_compile_options(cads_flags INTERFACE -Wall -Wextra -Wshadow -Wundef -Wno-unused-parameter)
endif()

add_library(cads_netx STATIC)
target_include_directories(cads_netx PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(cads_netx PRIVATE cads_flags)

# Portable on both targets - the host golden-byte test links this directly.
target_sources(cads_netx PRIVATE src/cads_netx_frame.c)

if(CMAKE_SYSTEM_NAME STREQUAL "Generic")
    target_sources(cads_netx PRIVATE src/cads_netx_rawio_board.c)
    # hal_eth_mac.h lives under targets/itsboard/hal; cads_hal_itsboard is
    # add_subdirectory'd after this module in the top-level CMakeLists.txt
    # (same ordering as modules/net), so the include path is listed directly
    # rather than taken from the target - mirrors modules/net/CMakeLists.txt.
    target_include_directories(cads_netx PRIVATE
        ${CADS_ROOT}/targets/itsboard
        ${CADS_ROOT}/targets/itsboard/hal
        ${CADS_ROOT}/core
        ${CADS_ROOT}/lib/CMSIS_6/CMSIS/Core/Include
        ${CADS_ROOT}/lib/cmsis_device_f4/Include
    )
    target_compile_definitions(cads_netx PRIVATE STM32F429xx CADS_TARGET_ITSBOARD=1)
else()
    target_sources(cads_netx PRIVATE src/cads_netx_rawio_sim.c)
endif()