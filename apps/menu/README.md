# `apps/menu` — the main menu

## What is it?

A `cads_menu` over four rows - Settings, About, GPIO, Network Info - each
`id` equal to the target's own view id, registered under
`CADS_VIEW_ID_MENU` (`0x0200`). Activating a row pushes that id and tells
`apps/desktop` an app was opened. `Back` is not handled here on purpose: it
falls through to the view dispatcher's default pop, which returns to whatever
pushed the menu (the desktop, in the normal path).

## Why is it shaped this way?

**It is the one place that has to know every app exists.** Every other app in
this milestone is reachable only by name (its view id) from here, so
`cads_menu_app_init()` also calls `cads_settings_init()`, `cads_about_init()`,
`cads_gpio_init()` and `cads_netinfo_init()` before building its own view.
This turns six separate calls the maintainer would otherwise have to remember
into two: this one and `cads_desktop_init()`.

**Item ids double as view ids.** `cads_menu_item_t.id` is hand
`cads_view_dispatcher_push()`ed straight through in the activation callback,
so there is exactly one number per app to get right rather than a mapping
table between "menu row" and "view to open".

## How do I use it?

```c
cads_desktop_init(&dispatcher);
cads_menu_app_init(&dispatcher); /* also registers settings/about/gpio/netinfo */
cads_view_dispatcher_push(&dispatcher, CADS_VIEW_ID_DESKTOP);
```

Linking `cads_app_menu` is enough to pull in `cads_app_settings`,
`cads_app_about`, `cads_app_gpio` and `cads_app_netinfo` too - see
`CMakeLists.txt`; only `cads_app_desktop` needs adding alongside it.

## What are the limits?

- The item table is a compile-time array. A menu whose contents change at
  run time (a plugin list, say) would need `cads_menu_set_items()` called
  from somewhere - nothing in this milestone needs that yet.
- `apps/bringup`'s explorer and input-probe tools are not in this menu; they
  are outside this app's scope and the maintainer's own bring-up path.
