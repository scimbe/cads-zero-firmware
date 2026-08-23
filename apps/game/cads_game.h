/*
 * CaDS Zero - Leo's Arcade: a select screen over four small games, each
 * playable with the board's own eight buttons.
 */

#ifndef CADS_GAME_H
#define CADS_GAME_H

#include <stdint.h>

#include "cads_view_dispatcher.h"

#define CADS_VIEW_ID_GAME          0x0800u /**< the select screen */
#define CADS_VIEW_ID_GAME_REFLEX   0x0801u
#define CADS_VIEW_ID_GAME_SNAKE    0x0802u
#define CADS_VIEW_ID_GAME_BREAKOUT 0x0803u
#define CADS_VIEW_ID_GAME_DODGER   0x0804u

/** Register the select screen and all four cartridges. */
void cads_game_init(cads_view_dispatcher_t* dispatcher);

/**
 * Advance whichever cartridge is currently on screen: Snake's fall
 * timer, Breakout's ball, Dodger's scroll, or the reflex test's "wait
 * for it" timer - none of these can advance from an input event alone,
 * the same reason cads_desktop_tick()/cads_gpio_tick() exist. Call this
 * once per main loop iteration, the same way those two already are; it
 * is a no-op until cads_game_init() has run and cheap (an id comparison)
 * whenever the arcade is not the view on screen at all.
 */
void cads_game_tick(uint32_t now_ms);

#endif /* CADS_GAME_H */
