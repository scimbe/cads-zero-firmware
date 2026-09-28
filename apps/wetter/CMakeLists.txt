# apps/wetter - weather station (lab L11). Needs apps/rnlab: the controller
# and display strings are L11's pure logic (cads_rnlab_logic), the HTTP
# client on the board is L10's (cads_app_rnlab). The simulator has no
# network, so it gets a backend that never fetches - same board/sim split
# as apps/netiperf.
if(CADS_TARGET STREQUAL "itsboard")
    set(CADS_APP_WETTER_NET_SOURCE cads_wetter_net_board.c)
else()
    set(CADS_APP_WETTER_NET_SOURCE cads_wetter_net_sim.c)
endif()

add_library(cads_app_wetter STATIC
    cads_wetter.c
    ${CADS_APP_WETTER_NET_SOURCE}
)

target_include_directories(cads_app_wetter PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(cads_app_wetter
    PUBLIC cads_gui_view cads_toolbox cads_net cads_rnlab_logic
    PRIVATE cads_flags)
if(CADS_TARGET STREQUAL "itsboard")
    target_link_libraries(cads_app_wetter PRIVATE cads_app_rnlab)
endif()
