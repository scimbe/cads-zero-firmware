# apps/netinfo - link state and PHY information.

add_library(cads_app_netinfo STATIC
    cads_netinfo.c
)

target_include_directories(cads_app_netinfo PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(cads_app_netinfo PUBLIC cads_gui_view cads_toolbox cads_net PRIVATE cads_flags)
