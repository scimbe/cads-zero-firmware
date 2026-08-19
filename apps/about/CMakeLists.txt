# apps/about - the board descriptor, on screen.

add_library(cads_app_about STATIC
    cads_about.c
)

target_include_directories(cads_app_about PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(cads_app_about PUBLIC cads_gui_view cads_toolbox PRIVATE cads_flags)
