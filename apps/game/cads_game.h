/*
 * CaDS Zero - Leo's Reflex Test: a small reaction-time game.
 */

#ifndef CADS_GAME_H
#define CADS_GAME_H

#include <stdint.h>

#include "cads_view_dispatcher.h"

#define CADS_VIEW_ID_GAME 0x0800u

/** Register the game's view. */
void cads_game_init(cads_view_dispatcher_t* dispatcher);

/**
 * Advance the round's random "wait for it" timer.
 *
 * Same reason cads_desktop_tick()/cads_gpio_tick() exist: the transition
 * from "armed" to "go" is the passage of time itself, not something the
 * input service can ever produce an event for - nobody pressed a key to
 * make the signal appear. Call this once per main loop iteration, the
 * same way those two already are; it is a no-op until cads_game_init()
 * has run and cheap (a few comparisons) whenever the game is not the
 * view on screen.
 */
void cads_game_tick(uint32_t now_ms);

#endif /* CADS_GAME_H */
