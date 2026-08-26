/*
 * CaDS Zero - the main menu: a scrolling list of the installed applications.
 *
 * Named cads_menu_app rather than cads_menu so that neither its header nor its
 * source shadows gui/widgets/cads_menu.h, which this app wraps.
 */

#ifndef CADS_MENU_APP_H
#define CADS_MENU_APP_H

#include "cads_view_dispatcher.h"

#define CADS_VIEW_ID_MENU 0x0200u

/**
 * Register the menu's view.
 *
 * Also registers apps/settings, apps/about, apps/gpio, apps/netinfo,
 * apps/filebrowser, apps/game and apps/netiperf - the menu's item table is
 * what turns their view ids into named rows, so it is the one place that
 * needs to know all seven exist. Call this after cads_desktop_init() and
 * before pushing CADS_VIEW_ID_DESKTOP.
 */
void cads_menu_app_init(cads_view_dispatcher_t* dispatcher);

#endif /* CADS_MENU_APP_H */
