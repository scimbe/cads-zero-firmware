# Widgets: drawing and input primitives with no knowledge of views or of the
# frame loop. They depend on the canvas and on the input event types only, which
# is what lets them be unit tested on the host without a compositor.

add_library(cads_gui_widgets STATIC
    cads_softkeys.c
    cads_statusbar.c
    cads_list.c
    cads_menu.c
    cads_dialog.c
    cads_textbox.c
)

target_include_directories(cads_gui_widgets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# cads_gui carries the canvas, the fonts and core/; cads_services carries
# services/ for "input/cads_input.h". Both are PUBLIC because the widget headers
# include from them.
target_link_libraries(cads_gui_widgets PUBLIC cads_gui cads_services PRIVATE cads_flags)
