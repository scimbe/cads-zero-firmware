# apps/game - Leo's Reflex Test.

add_library(cads_app_game STATIC
    cads_game.c
)

target_include_directories(cads_app_game PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(cads_app_game PUBLIC cads_gui_view cads_toolbox PRIVATE cads_flags)
