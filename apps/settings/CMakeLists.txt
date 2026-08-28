# apps/settings - brightness, SPI clock, touch calibration entry, factory reset.

add_library(cads_app_settings STATIC
    cads_settings.c
    cads_touch_calib.c
)

target_include_directories(cads_app_settings PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(cads_app_settings PUBLIC cads_gui_view cads_toolbox cads_storage cads_config cads_net cads_wifi PRIVATE cads_flags)

# The "Join WiFi" row calls cads_marauder_join() (apps/marauder, reached via a
# relative #include - see cads_settings.c's own comment on why) - only
# actually needed, and only actually built, when that optional app is on.
if(CADS_APP_MARAUDER)
    target_link_libraries(cads_app_settings PRIVATE cads_app_marauder)
endif()
