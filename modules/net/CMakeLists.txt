# CaDS Zero - net module (RMII MAC + lwIP netif on the board, an honest
# "no link" stub on the simulator).
#
# Unlike modules/storage there is nothing here for host tests to exercise:
# hal_eth_mac.c is a real DMA driver over real silicon, and cads_net_sim.c
# (see its own file header) deliberately does not pretend otherwise. This
# module therefore builds one of two single-file implementations depending
# on target, the same target switch as the top-level CMakeLists.txt uses,
# duplicated here for the same reason modules/storage duplicates it: standalone
# configurability is not needed for this module today, but matching the
# existing pattern keeps every feature module readable the same way.

if(NOT DEFINED CADS_ROOT)
    get_filename_component(CADS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
endif()

if(NOT TARGET cads_flags)
    add_library(cads_flags INTERFACE)
    target_compile_options(cads_flags INTERFACE -Wall -Wextra -Wshadow -Wundef -Wno-unused-parameter)
endif()

add_library(cads_net STATIC)
target_include_directories(cads_net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(cads_net PRIVATE cads_flags)

if(CMAKE_SYSTEM_NAME STREQUAL "Generic")
    if(NOT EXISTS ${CADS_ROOT}/lib/lwip/src/Filelists.cmake)
        message(FATAL_ERROR "lib/lwip is empty. Run: git submodule update --init lib/lwip")
    endif()

    # A real fix (lib/patches/lwip-lwiperf-abort-fix.patch - see that file's
    # own commit-message-turned-comment for the two actual bugs: a dangling
    # list head that corrupted lwIP's heap, and a leaked tcp_pcb) that used
    # to live only as a commit on this vendored copy - 2026-08-30, moved to
    # a plain patch applied here instead of depending on a fork/mirror to
    # host that commit, at the maintainer's own request (one fewer external
    # repo to keep in sync). Idempotent: checks for the fix's own marker
    # line before applying, so a re-configure after the patch already
    # landed is a no-op rather than a "does not apply" failure - same
    # pattern tools/marauder-build/build_and_flash.sh's own source patches
    # already use for exactly this reason.
    set(LWIPERF_C ${CADS_ROOT}/lib/lwip/src/apps/lwiperf/lwiperf.c)
    file(STRINGS ${LWIPERF_C} LWIPERF_FIX_MARKER REGEX "lwiperf_all_connections = i;")
    if(NOT LWIPERF_FIX_MARKER)
        message(STATUS "Applying lib/patches/lwip-lwiperf-abort-fix.patch to lib/lwip")
        execute_process(
            COMMAND git apply ${CADS_ROOT}/lib/patches/lwip-lwiperf-abort-fix.patch
            WORKING_DIRECTORY ${CADS_ROOT}/lib/lwip
            RESULT_VARIABLE LWIPERF_PATCH_RESULT
            ERROR_VARIABLE LWIPERF_PATCH_ERROR
        )
        if(NOT LWIPERF_PATCH_RESULT EQUAL 0)
            message(FATAL_ERROR
                "lib/patches/lwip-lwiperf-abort-fix.patch failed to apply "
                "(lib/lwip's pinned commit probably moved) - re-check by hand:\n"
                "${LWIPERF_PATCH_ERROR}")
        endif()
        file(STRINGS ${LWIPERF_C} LWIPERF_FIX_MARKER REGEX "lwiperf_all_connections = i;")
        if(NOT LWIPERF_FIX_MARKER)
            message(FATAL_ERROR
                "lib/patches/lwip-lwiperf-abort-fix.patch applied cleanly but its own "
                "fix marker is still missing from ${LWIPERF_C} - the patch itself needs a look")
        endif()
    endif()

    # lwIP's own file lists (lwipcore/lwipcore4/lwipnetif) - only the pieces
    # this build actually turns on in lwipopts.h: core + IPv4 + the Ethernet
    # netif glue (ethernet.c). No lwipcore6 (LWIP_IPV6=0), no lwipapi
    # (LWIP_NETCONN=LWIP_SOCKET=0 - raw API only). One app, lwipiperf -
    # apps/bringup/explorer_iperf_demo.c's lwiperf_start_tcp_server_default()
    # call needs it; every other lwipallapps_SRCS entry (httpd, mdns, tftp,
    # ...) stays out, this firmware hand-rolls its own HTTP/CLI/screencast
    # protocols instead (see those files' own headers for why).
    set(LWIP_DIR ${CADS_ROOT}/lib/lwip)
    include(${LWIP_DIR}/src/Filelists.cmake)

    # lwIP is vendored, not ours - like lib/littlefs and lib/Unity, its
    # warnings are not ours to fix, so it gets its own library without
    # cads_flags (see modules/storage/CMakeLists.txt's cads_littlefs for the
    # same reasoning).
    # lwipppp_SRCS: modules/wifi's PPPoS link to the ESP32 co-processor (see
    # docs/reference/wifi-coprocessor.md). Most of its files compile down to
    # nearly nothing - CHAP/PAP/MPPE/CCP/multilink/PPPoE/PPPoL2TP are all off
    # in lwipopts.h, this link has no use for any of them (it is a private,
    # physically-wired point-to-point link, not a dial-up/VPN scenario) - but
    # they are still part of lwIP's own Filelists.cmake grouping, and pulling
    # in the whole group (same as every other lwipXXX_SRCS list here) keeps
    # this file matching lwIP's own structure rather than hand-picking files
    # lwIP's own maintainers regroup from release to release.
    add_library(cads_lwip STATIC
        ${lwipcore_SRCS} ${lwipcore4_SRCS} ${lwipnetif_SRCS} ${lwipiperf_SRCS} ${lwipppp_SRCS})
    target_include_directories(cads_lwip PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include       # lwipopts.h, arch/cc.h
        ${LWIP_DIR}/src/include
    )
    # arch/cc.h includes core/cads_hal.h (LWIP_PLATFORM_ASSERT -> cads_hal_panic) -
    # the only reason lwIP's own sources need anything from this firmware at
    # all, and cads_hal.h itself is target-neutral (no device headers), so
    # nothing from targets/itsboard belongs on this list.
    target_include_directories(cads_lwip PRIVATE ${CADS_ROOT}/core)

    target_sources(cads_net PRIVATE src/cads_net_board.c)
    # Mirrors modules/storage/CMakeLists.txt's CADS_STORAGE_TARGET_INCLUDES:
    # cads_hal_itsboard (targets/itsboard/CMakeLists.txt) is not a link
    # dependency here because it is add_subdirectory'd after modules/net in
    # the top-level CMakeLists.txt, so its target does not exist yet at this
    # point - the include paths and defines it would have carried are listed
    # directly instead, same as storage already does for the same reason.
    target_include_directories(cads_net PRIVATE
        ${CADS_ROOT}/targets/itsboard
        ${CADS_ROOT}/targets/itsboard/hal
        ${CADS_ROOT}/core
        ${CADS_ROOT}/lib/CMSIS_6/CMSIS/Core/Include
        ${CADS_ROOT}/lib/cmsis_device_f4/Include
    )
    target_compile_definitions(cads_net PRIVATE STM32F429xx CADS_TARGET_ITSBOARD=1)
    target_link_libraries(cads_net PUBLIC cads_lwip)
else()
    target_sources(cads_net PRIVATE src/cads_net_sim.c)
endif()
