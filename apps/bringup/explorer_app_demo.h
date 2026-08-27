#ifndef CADS_EXPLORER_APP_DEMO_H
#define CADS_EXPLORER_APP_DEMO_H

#include <stdint.h>

/**
 * Boot the real application tree - desktop, menu, settings (+ its confirm
 * dialog), about, gpio, netinfo - exactly as the maintainer's production boot
 * sequence eventually will, and hold it live on the panel for `seconds`.
 *
 * This is the M3 hardware-gate rehearsal: desktop -> menu -> an app is three
 * navigation levels deep, reachable by button or by touch identically (the
 * property gui/widgets/README.md promises). Unlike explorer_gui_demo.c this
 * enables the status bar and soft-key strip, because every one of these
 * views declares a title and keys meant to be shown.
 *
 * What this can verify without a human: that the tree builds, boots, and
 * survives `seconds` of ticking without a crash or a lost task, and how many
 * navigation transitions occurred (cads_view_dispatcher_generation() before
 * and after) if the operator drove it by button or touch during the window.
 * What it cannot verify by itself: that a human actually saw a lion, read a
 * menu, and reached a leaf app *by touch specifically* - see docs/ROADMAP.md
 * M3 for why that stays an open item until someone looks at the panel.
 */
/* seconds == 0: run until a console byte arrives; that byte is returned so
 * the caller can treat it as the first character of the next command
 * (scripted one-shot commands would otherwise lose their command letter to
 * the wake-up). Returns 0 when the run ended by timeout. */
uint8_t cads_explorer_app_demo(uint32_t seconds);

#endif /* CADS_EXPLORER_APP_DEMO_H */
