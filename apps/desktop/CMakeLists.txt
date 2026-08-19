# apps/desktop - the home screen and Leo the lion's residence.

add_library(cads_app_desktop STATIC
    cads_desktop.c
)

target_include_directories(cads_app_desktop PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(cads_app_desktop PUBLIC cads_gui_view cads_toolbox PRIVATE cads_flags)
