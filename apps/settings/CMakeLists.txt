# apps/settings - brightness, SPI clock, touch calibration entry, factory reset.

add_library(cads_app_settings STATIC
    cads_settings.c
)

target_include_directories(cads_app_settings PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(cads_app_settings PUBLIC cads_gui_view PRIVATE cads_flags)
