/*
 * CaDS Zero - the real application tree, live on the panel, reached from the
 * hardware explorer's 'd' command.
 *
 * Same non-reentrancy rule as explorer_gui_demo.c: this never calls
 * cads_input_tick() itself. The input task already polls it at 100 Hz under
 * the scheduler; this only redirects that already-running task's events to
 * the GUI for as long as it owns the screen (cads_gui_attach_input()) and
 * hands the input task's own callback back on the way out.
 */

#include "explorer_app_demo.h"

#include "cads/net/net.h"
#include "cads_desktop.h"
#ifdef CADS_APP_GAME_ENABLED
#include "cads_game.h"
#endif
#ifdef CADS_APP_GPIO_ENABLED
#include "cads_gpio.h"
#endif
#ifdef CADS_APP_NETTOOLS_ENABLED
#include "cads_nettools.h"
#endif
#ifdef CADS_APP_ACTIVE_ENABLED
#include "cads_active.h"
#endif
#ifdef CADS_APP_SETTINGS_ENABLED
#include "cads_settings.h"
#include "cads_touch_calib.h"
#endif
#include "cads_gui.h"
#include "cads_hal.h"
#include "cads_menu_app.h" /* also registers settings, about, gpio, netinfo, filebrowser, game */
#include "cads_softkeys.h"
#include "cads_statusbar.h"
#include "cads_view_dispatcher.h"
#include "explorer_eth.h" /* cads_explorer_net_mac() */
#include "input_probe.h" /* cads_probe_puts / cads_probe_put_uint */

/*
 * desktop, menu, settings, settings-confirm, about, gpio, netinfo,
 * filebrowser, filebrowser-info, game (the arcade's own select screen),
 * game-reflex, game-snake, game-breakout, game-dodger - 14 registrations
 * total.
 *
 * This constant was 7 (only accounting for desktop through netinfo) from
 * when this file was first wired to the full app tree, and was never
 * updated when apps/filebrowser (2 views) was added later in M4 - found
 * while adding this game's own view, not by this bullet's own testing.
 * cads_view_dispatcher_add() fails silently past capacity (returns
 * false, every caller here discards it with `(void)`), so with the old
 * value of 7 the LAST TWO registration calls in this file's own startup
 * order - filebrowser's info view, and cads_menu_app_init()'s own MENU
 * view itself - never actually registered. That means CADS_VIEW_ID_MENU
 * was never findable in this file's dispatcher: pressing OK on the
 * desktop to open the menu (cads_view_dispatcher_push()) would have
 * failed silently every single time the `d` command ran, on every
 * hardware check this whole session has done with it - none of those
 * checks ever pressed a button to actually exercise that path (see
 * tests/unit/test_app_tree.c, added alongside this fix, for a host-side
 * regression guard: it feeds a synthetic OK keypress through the exact
 * same dispatcher/menu code and asserts the menu view actually becomes
 * current, so this class of bug fails a build next time rather than
 * requiring a human at the panel to notice a dead OK button).
 *
 * Bumped from 10 to 14 when Leo's Arcade grew from one game view to a
 * select screen plus four pushed cartridges (see apps/game/cads_game.c's
 * own file header for why each cartridge needs a real view rather than
 * one view with an internal mode) - the exact same failure mode this
 * comment already describes once, caught this time by updating both this
 * constant and tests/unit/test_app_tree.c's mirrored one together rather
 * than by a human noticing a dead cartridge on the panel.
 *
 * Bumped from 14 to 16 when apps/netiperf added its two views (server,
 * client) - the fully-saturated-at-14 count meant those two were exactly
 * the ones silently dropped this time (test_app_tree.c caught it on host
 * before it ever reached hardware, same regression guard as above).
 *
 * Bumped from 22 to 24 when apps/active (M9 Active Net Tools) added its
 * two views (the 0x0B00 selector + the one 0x0B01 shared tool view - the
 * suite deliberately collapses seven tools into one shared view to stay
 * inside the 256 B RAM margin, see apps/active/cads_active.h). Same guard:
 * tests/unit/test_app_tree.c's mirrored capacity and its find-asserts for
 * CADS_VIEW_ID_ACTIVE/_TOOL catch a silent drop on host before hardware.
 */
#define CADS_APP_DEMO_VIEW_CAPACITY 26u
#define CADS_APP_DEMO_STACK_DEPTH   4u

static cads_view_entry_t s_entries[CADS_APP_DEMO_VIEW_CAPACITY];
static uint32_t s_stack[CADS_APP_DEMO_STACK_DEPTH];
static cads_view_dispatcher_t s_dispatcher;
static cads_gui_t s_gui;
static cads_statusbar_t s_statusbar;
static cads_softkeys_t s_softkeys;

/*
 * Slot 0 is the rightmost cell (cads_statusbar.h's own numbering) and is
 * this firmware's first indicator wired up at all - storage/battery/clock
 * are still unclaimed slots 1..3 for whoever builds those next.
 *
 * Returns a literal string constant, never a formatted buffer:
 * cads_statusbar_set_indicator() marks a slot dirty by comparing the
 * pointer it is given against the one it already has (cads_statusbar.h -
 * "Setting a slot to the pointer it already holds is free and marks
 * nothing dirty"), so reusing the SAME literal address for an unchanged
 * state is what makes that comparison work; a scratch buffer reformatted
 * every call would report a fresh pointer - and therefore fresh damage -
 * every single tick even when nothing changed.
 */
#define CADS_NET_STATUSBAR_SLOT 0u

static const char* cads_net_indicator_text(void) {
    cads_net_status_t net;
    cads_net_status(&net);
    if(!net.link_up) return "no link";
    if(net.ip_addr == 0u) return "no lease";
    return net.speed_mbit >= 100u ? "100M" : "10M";
}

uint8_t cads_explorer_app_demo(uint32_t seconds) {
    cads_net_init(cads_explorer_net_mac());

    cads_view_dispatcher_init(
        &s_dispatcher, s_entries, CADS_APP_DEMO_VIEW_CAPACITY, s_stack, CADS_APP_DEMO_STACK_DEPTH);

    cads_rect_t full = {0, 0, CADS_CANVAS_WIDTH, CADS_CANVAS_HEIGHT};
    cads_view_dispatcher_set_area(&s_dispatcher, full);

    cads_desktop_init(&s_dispatcher);
    cads_menu_app_init(&s_dispatcher); /* also registers settings, about, gpio, netinfo */

    if(!cads_view_dispatcher_switch_to(&s_dispatcher, CADS_VIEW_ID_DESKTOP)) {
        cads_probe_puts("# app demo: failed to switch to the desktop\r\n");
        return 0u;
    }

    cads_statusbar_init(&s_statusbar);
    cads_softkeys_init(&s_softkeys);
    cads_gui_init(&s_gui, &s_dispatcher, &s_statusbar, &s_softkeys);
    cads_gui_attach_input(&s_gui);

    uint32_t start_generation = cads_view_dispatcher_generation(&s_dispatcher);

    if(seconds == 0u) {
        cads_probe_puts(
            "# app demo: desktop -> menu -> app live on the panel until a "
            "console key arrives - OK opens the menu, F1 pets Leo, "
            "every action here is reachable by touch too\r\n");
    } else {
        cads_probe_puts("# app demo: desktop -> menu -> app live on the panel for ");
        cads_probe_put_uint(seconds);
        cads_probe_puts(
            "s - OK opens the menu, F1 pets Leo, "
            "every action here is reachable by touch too\r\n");
    }

    uint32_t start = cads_hal_ticks_ms();
    uint32_t total_pixels = 0u;
    uint32_t frames = 0u;

    /* seconds == 0: interactive session - run until a console byte arrives
     * (the byte is consumed; any key drops back to the explorer prompt).
     * This is what boot.autostart uses to hand the panel to the menu at
     * power-on while keeping the console reachable. */
    uint8_t wake_byte = 0u;
    for(;;) {
        uint32_t now = cads_hal_ticks_ms();
        if(seconds == 0u) {
            uint8_t byte;
            if(cads_hal_console_read(&byte)) {
                wake_byte = byte;
                break;
            }
        } else if(now - start >= seconds * 1000u) {
            break;
        }
        /* A promiscuous-capture M9 tool (802.1X sniff, TCP RST daemon) owns
         * the RX ring for its duration and has suppressed lwIP's own poll via
         * cads_net_set_poll_suppressed(true) - so this loop must not also
         * drive cads_net_poll() while that session is active, or the two
         * would fight over the same RX descriptors. cads_active_owns_rx()
         * reports exactly that state; the TX-only and lwIP-RX tools never
         * claim the ring, so for them this is unchanged. */
#ifdef CADS_APP_ACTIVE_ENABLED
        if(!cads_active_owns_rx())
#endif
            cads_net_poll();
        cads_statusbar_set_indicator(&s_statusbar, CADS_NET_STATUSBAR_SLOT, cads_net_indicator_text());
        cads_desktop_tick(now);
#ifdef CADS_APP_GPIO_ENABLED
        cads_gpio_tick(now);
#endif
#ifdef CADS_APP_GAME_ENABLED
        cads_game_tick(now);
#endif
#ifdef CADS_APP_NETTOOLS_ENABLED
        cads_nettools_tick(now);
#endif
#ifdef CADS_APP_ACTIVE_ENABLED
        cads_active_tick(now);
#endif
#ifdef CADS_APP_SETTINGS_ENABLED
        cads_touch_calib_tick(now);
        cads_settings_service_config(); /* console-task storage owner services reload requests */
#endif
        uint32_t pixels = cads_gui_tick(&s_gui, now);
        if(pixels) {
            total_pixels += pixels;
            frames++;
        }
        cads_hal_delay_ms(10u);
    }

    uint32_t end_generation = cads_view_dispatcher_generation(&s_dispatcher);
    cads_gui_detach_input();

    cads_probe_puts("# app demo done: ");
    cads_probe_put_uint(frames);
    cads_probe_puts(" frames flushed, ");
    cads_probe_put_uint(total_pixels);
    cads_probe_puts(" pixels total, ");
    cads_probe_put_uint(end_generation - start_generation);
    cads_probe_puts(" navigation transitions, ended on view id ");
    cads_probe_put_uint(cads_view_dispatcher_current_id(&s_dispatcher));
    cads_probe_puts("\r\n");
    return wake_byte;
}
