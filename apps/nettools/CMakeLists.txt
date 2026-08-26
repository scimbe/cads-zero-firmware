# apps/nettools - the network Swiss-army-knife's GUI face.
#
# One portable source, no board/sim split: everything goes through
# modules/net's cads_net_* API, whose simulator half already answers
# honestly ("never resolves", "no link") - see cads_nettools.h.

add_library(cads_app_nettools STATIC
    cads_nettools.c
)

target_include_directories(cads_app_nettools PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(cads_app_nettools PUBLIC cads_gui_view cads_gui_widgets cads_toolbox cads_net PRIVATE cads_flags)
