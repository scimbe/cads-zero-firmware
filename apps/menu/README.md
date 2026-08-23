# `apps/menu` — the main menu

## What is it?

A `cads_menu` over up to six rows - Settings, About, GPIO, Network Info,
Files, Reflex Test - each `id` equal to the target's own view id, registered
under `CADS_VIEW_ID_MENU` (`0x0200`). Activating a row pushes that id and
tells `apps/desktop` an app was opened. `Back` is not handled here on
purpose: it falls through to the view dispatcher's default pop, which
returns to whatever pushed the menu (the desktop, in the normal path).

Every row is individually optional at build time: each is wrapped in its own
`#ifdef CADS_APP_<NAME>_ENABLED`, set by the matching `CADS_APP_<NAME>`
CMake option (default `ON`) in the top-level `CMakeLists.txt`. Turning one
off drops both the row and the `_init()` call that would have registered its
view - not just hides the row from a menu whose view still exists - which is
what lets a RAM-constrained build shed whole apps instead of just their entry
points. See the `minimal` leg of the CI matrix (`.github/workflows/ci.yml`)
for a build with every optional app off.

## Why is it shaped this way?

**It is the one place that has to know every app exists.** Every other app in
this milestone is reachable only by name (its view id) from here, so
`cads_menu_app_init()` also calls each enabled app's own `_init()` - up to
`cads_settings_init()`, `cads_about_init()`, `cads_gpio_init()`,
`cads_netinfo_init()`, `cads_filebrowser_init()` and `cads_game_init()` -
before building its own view. This turns what would otherwise be up to seven
separate calls the maintainer has to remember into two: this one and
`cads_desktop_init()`.

**Item ids double as view ids.** `cads_menu_item_t.id` is hand
`cads_view_dispatcher_push()`ed straight through in the activation callback,
so there is exactly one number per app to get right rather than a mapping
table between "menu row" and "view to open".

## How do I use it?

```c
cads_desktop_init(&dispatcher);
cads_menu_app_init(&dispatcher); /* also registers whichever apps are enabled */
cads_view_dispatcher_push(&dispatcher, CADS_VIEW_ID_DESKTOP);
```

Linking `cads_app_menu` is enough to pull in every currently-enabled app's own
library too - `apps/menu/CMakeLists.txt` conditionally links
`cads_app_settings`/`_about`/`_gpio`/`_netinfo`/`_filebrowser`/`_game`, one
`target_link_libraries()` guarded by the same `CADS_APP_<NAME>_ENABLED` check
as the row and the `_init()` call above; only `cads_app_desktop` needs adding
alongside it.

## What are the limits?

- The item table is a compile-time array. A menu whose contents change at
  run time (a plugin list, say) would need `cads_menu_set_items()` called
  from somewhere - nothing in this milestone needs that yet.
- `apps/bringup`'s explorer and input-probe tools are not in this menu; they
  are outside this app's scope and the maintainer's own bring-up path.
