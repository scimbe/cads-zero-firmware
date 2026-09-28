# apps/rnlab - computer-networks lab framework (branch praktikum/start).
#
# Two libraries, so the host can test lesson logic without lwIP:
#   cads_rnlab_logic  every *_logic.c - pure C, both targets, host-tested
#                     by tests/unit/test_rnlab_*.c
#   cads_app_rnlab    the `lab` dispatcher plus the lessons' board files
#                     (lNN_<slug>.c, may use lwIP); the simulator gets
#                     rnlab_lessons_sim.c in their place.
#
# Every lesson's files are listed here up front, so a lesson branch only
# ever edits its own lNN_* files and tests/unit/test_rnlab_lNN.c - never
# this file, the dispatcher, or tests/unit/CMakeLists.txt.

set(RNLAB_LESSONS
    l01_schichten_kapselung
    l02_ethernet_arp
    l03_ipv4_subnetting
    l04_icmp
    l05_dhcp
    l06_dns_nat
    l07_udp_transport
    l08_tcp_flusskontrolle
    l09_congestion_control
    l10_http_wetter_1
    l11_wetter_app
)

set(RNLAB_LOGIC_SOURCES src/rnlab_args_logic.c)
set(RNLAB_BOARD_SOURCES)
foreach(lesson ${RNLAB_LESSONS})
    list(APPEND RNLAB_LOGIC_SOURCES src/${lesson}_logic.c)
    list(APPEND RNLAB_BOARD_SOURCES src/${lesson}.c)
endforeach()

add_library(cads_rnlab_logic STATIC ${RNLAB_LOGIC_SOURCES})
target_include_directories(cads_rnlab_logic PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(cads_rnlab_logic PUBLIC cads_toolbox PRIVATE cads_flags)

# The driver's hook points (cads/net/rnlab_hooks.h), fanned out to the
# lessons' rnlab_lNN_hook_*(). A separate library because modules/net links it
# (the driver and lwIP's ip4.c are what reference it); portable, so the host
# test links it too. Only the headers of cads_cli/cads_net are needed, hence
# plain include paths rather than links back into those libraries.
add_library(cads_rnlab_hooks STATIC src/rnlab_hooks.c)
target_include_directories(cads_rnlab_hooks PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CADS_ROOT}/modules/net/include
    ${CADS_ROOT}/modules/cli/include)
target_link_libraries(cads_rnlab_hooks PRIVATE cads_flags)

add_library(cads_app_rnlab STATIC src/rnlab.c)
if(CADS_TARGET STREQUAL "itsboard")
    target_sources(cads_app_rnlab PRIVATE ${RNLAB_BOARD_SOURCES} src/rnlab_selftest_board.c)
else()
    target_sources(cads_app_rnlab PRIVATE src/rnlab_lessons_sim.c)
endif()
target_include_directories(cads_app_rnlab
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    # core/cads_hal.h (ticks, console write) - same reason as modules/cli.
    PRIVATE ${CADS_ROOT}/core)
target_link_libraries(cads_app_rnlab PUBLIC cads_rnlab_logic cads_cli cads_net cads_toolbox PRIVATE cads_flags)
