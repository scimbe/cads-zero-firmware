# apps/settings - brightness, SPI clock, touch calibration entry, factory reset.

add_library(cads_app_settings STATIC
    cads_settings.c
    cads_touch_calib.c
)

target_include_directories(cads_app_settings PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(cads_app_settings PUBLIC cads_gui_view cads_toolbox cads_storage cads_config cads_net cads_wifi PRIVATE cads_flags)
