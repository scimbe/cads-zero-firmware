# CaDS Zero - cads_cli: one shared command table, two transports.
#
# cads_cli.c is fully portable (target-neutral, same as cads/net/net.h) and
# builds identically for both targets. The TCP transport does not - it needs
# lwIP - so it gets the same board/host split cads_net_board.c/
# cads_net_sim.c already established: cads_cli_tcp_board.c on the board,
# cads_cli_tcp_sim.c an honest stub on the simulator.

if(NOT DEFINED CADS_ROOT)
    get_filename_component(CADS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
endif()

if(NOT TARGET cads_flags)
    add_library(cads_flags INTERFACE)
    target_compile_options(cads_flags INTERFACE -Wall -Wextra -Wshadow -Wundef -Wno-unused-parameter)
endif()

add_library(cads_cli STATIC
    src/cads_cli.c
    src/cads_cli_stream.c
)
target_include_directories(cads_cli
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    # core/cads_hal.h (cads_hal_ticks_ms()) - neither cads_net nor
    # cads_toolbox expose core/ publicly (cads_net's own include of it is
    # PRIVATE; cads_toolbox never needed it at all), so cads_cli.c needs its
    # own path to it.
    PRIVATE ${CADS_ROOT}/core)
target_link_libraries(cads_cli PRIVATE cads_flags PUBLIC cads_net cads_toolbox)

if(CMAKE_SYSTEM_NAME STREQUAL "Generic")
    # cads_cli_tcp_board.c includes lwip/tcp.h directly. No extra include
    # path needed for it: linking cads_net PUBLIC above already brings in
    # cads_lwip's PUBLIC include dirs (lwIP's own headers, and
    # modules/net/include for lwipopts.h/arch/cc.h) transitively.
    target_sources(cads_cli PRIVATE src/cads_cli_tcp_board.c)
else()
    target_sources(cads_cli PRIVATE src/cads_cli_tcp_sim.c)
endif()
