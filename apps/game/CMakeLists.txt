# apps/game - Leo's Arcade: a select screen over four small games.

add_library(cads_app_game STATIC
    cads_game.c
    cads_game_reflex.c
    cads_game_snake.c
    cads_game_breakout.c
    cads_game_dodger.c
)

target_include_directories(cads_app_game PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(cads_app_game PUBLIC cads_gui_view cads_toolbox PRIVATE cads_flags)
