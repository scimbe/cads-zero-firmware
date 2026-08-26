# apps/netiperf - iperf throughput server and client, as GUI apps.
#
# Two implementations, same split as apps/bringup/explorer_iperf_demo.c's own
# board/sim pair: the board one is real glue over lwIP's lwiperf app; the
# simulator has no lwipiperf sources compiled in at all (modules/net/
# CMakeLists.txt only builds cads_lwip - and therefore lwiperf.h's symbols -
# for CADS_TARGET STREQUAL "itsboard"), so cads_netiperf_sim.c gives the same
# two views a "not available in the simulator" message instead of linking
# against something that does not exist on host.
if(CADS_TARGET STREQUAL "itsboard")
    set(CADS_APP_NETIPERF_SOURCE cads_netiperf.c)
else()
    set(CADS_APP_NETIPERF_SOURCE cads_netiperf_sim.c)
endif()

add_library(cads_app_netiperf STATIC
    ${CADS_APP_NETIPERF_SOURCE}
)

target_include_directories(cads_app_netiperf PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(cads_app_netiperf PUBLIC cads_gui_view cads_toolbox cads_net PRIVATE cads_flags)
