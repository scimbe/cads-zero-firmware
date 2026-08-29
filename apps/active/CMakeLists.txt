# apps/active - the M9 "Active Net Tools" offensive suite's GUI face.
#
# One portable source, no board/sim split (the same call shape as
# apps/nettools): every send goes through modules/netx rawio, whose
# simulator half honestly no-ops (false/0 - see cads_netx_rawio_sim.c),
# and every capture session is a no-op on host. The UI therefore builds
# and runs on both targets; the host gallery exercises the navigation and
# confirm-gate UX without any packets moving, exactly as the simulator
# already does for the rest of the net stack.

add_library(cads_app_active STATIC
    cads_active.c
)

target_include_directories(cads_app_active PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(cads_app_active PUBLIC
    cads_gui_view
    cads_gui_widgets
    cads_toolbox
    cads_netx
    cads_net
    cads_config
    PRIVATE cads_flags)