# apps/menu - the main menu. Registers whichever of apps/settings,
# apps/about, apps/gpio, apps/netinfo, apps/filebrowser and apps/game are
# actually enabled (see top-level CMakeLists.txt's own "optional apps"
# section and cads_menu_app.c's own CADS_APP_*_ENABLED guards), alongside
# apps/desktop, which is never optional. The CADS_APP_*_ENABLED compile
# definitions themselves come from cads_flags (top-level CMakeLists.txt),
# not from here - this file only needs to decide what to link.

add_library(cads_app_menu STATIC
    cads_menu_app.c
)

target_include_directories(cads_app_menu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(cads_app_menu
    PUBLIC cads_gui_view
    PRIVATE
        cads_app_desktop
        cads_flags
)

# Each optional app is linked in only when its own CADS_APP_* option
# (top-level CMakeLists.txt) is ON. The library is never even built when
# its option is OFF (that CMakeLists.txt's add_subdirectory() call is
# itself skipped), so there is nothing here to link even by mistake.
if(CADS_APP_SETTINGS)
    target_link_libraries(cads_app_menu PRIVATE cads_app_settings)
endif()
if(CADS_APP_ABOUT)
    target_link_libraries(cads_app_menu PRIVATE cads_app_about)
endif()
if(CADS_APP_GPIO)
    target_link_libraries(cads_app_menu PRIVATE cads_app_gpio)
endif()
if(CADS_APP_NETINFO)
    target_link_libraries(cads_app_menu PRIVATE cads_app_netinfo)
endif()
if(CADS_APP_FILEBROWSER)
    target_link_libraries(cads_app_menu PRIVATE cads_app_filebrowser)
endif()
if(CADS_APP_GAME)
    target_link_libraries(cads_app_menu PRIVATE cads_app_game)
endif()
