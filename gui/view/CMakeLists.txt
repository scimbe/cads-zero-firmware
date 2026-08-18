# The view framework: view state, the navigation stack and the compositor.
# Depends on the widgets because the compositor owns the status bar and the
# soft-key strip. Nothing here depends on a target header, so the same objects
# link against either HAL backend.

add_library(cads_gui_view STATIC
    cads_view.c
    cads_view_dispatcher.c
    cads_gui.c
)

target_include_directories(cads_gui_view PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(cads_gui_view PUBLIC cads_gui_widgets PRIVATE cads_flags)
