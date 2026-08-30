# apps/marauder - the ESP32Marauder co-processor CLI bridge's GUI face.
#
# One portable source: cads_hal_wifi_uart_* (core/cads_hal.h) already has a
# board driver (targets/itsboard/hal/hal_uart_wifi.c) and an honest no-op
# host stub (targets/sim/hal_sim.c), the same split modules/wifi's board/sim
# implementations already established - so this app needs no board/sim split
# of its own, only the line-reader/menu/view logic, which is fully portable.

add_library(cads_app_marauder STATIC
    cads_marauder.c
    cads_marauder_reader.c
    cads_marauder_join.c
    cads_marauder_pcap.c
)

target_include_directories(cads_app_marauder PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(cads_app_marauder PUBLIC
    cads_gui_view
    cads_gui_widgets
    cads_toolbox
    cads_net
    cads_config
    PRIVATE cads_flags)
