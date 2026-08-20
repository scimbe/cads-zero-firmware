# apps/menu - the main menu. Registers apps/settings, apps/about, apps/gpio,
# apps/netinfo and apps/filebrowser too (see cads_menu_app.h), so linking
# this one library pulls in the whole M4/M6 app set alongside apps/desktop.

add_library(cads_app_menu STATIC
    cads_menu_app.c
)

target_include_directories(cads_app_menu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(cads_app_menu
    PUBLIC cads_gui_view
    PRIVATE
        cads_app_desktop
        cads_app_settings
        cads_app_about
        cads_app_gpio
        cads_app_netinfo
        cads_app_filebrowser
        cads_flags
)
