# apps/gpio - drive OUT0..15, watch IN0..7 and INT0..5 live.

add_library(cads_app_gpio STATIC
    cads_gpio.c
)

target_include_directories(cads_app_gpio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(cads_app_gpio PUBLIC cads_gui_view cads_toolbox PRIVATE cads_flags)
