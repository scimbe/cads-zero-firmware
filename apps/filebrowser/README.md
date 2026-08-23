# `apps/filebrowser` — read-only navigation of the littlefs volume

## What is it?

Two views registered with the dispatcher: a `cads_menu` (`CADS_VIEW_ID_FILEBROWSER`,
`0x0700`) that lists whatever directory the app's current path points to, and a
modal info dialog (`CADS_VIEW_ID_FILEBROWSER_INFO`, `0x0701`) pushed on top of
it when the selected row is a file. `Up`/`Down` move the selection, `OK` opens a
directory or shows a file's size, `Back` walks up one directory level and, once
the path is already `/`, is left unconsumed so the dispatcher pops the whole
app. Every listing comes from `cads_storage_dir_open()` /
`cads_storage_dir_read()` in `cads/storage/storage.h`; this app is a lens over
that volume, not a file manager — nothing here creates, renames or deletes
anything.

## Why is it shaped this way?

**Read-only, deliberately.** `gui/widgets` has no text entry, and this
milestone's job was the storage layer and a way to see what is on it, not a
full file manager. Rename, delete and a content viewer are all reachable
through `cads/storage/storage.h` already (`cads_storage_remove()`,
`cads_storage_rename()`, `cads_storage_open()`); nothing in this app stops a
later one from adding them.

**One menu, a mutable path, no per-depth views.** A view per directory depth
would need dynamic view ids for an unbounded tree. Instead there is a single
`cads_filebrowser_t` whose `path` field is a fixed `CADS_STORAGE_PATH_MAX + 1`
buffer, reloaded from storage every time the user goes up (`OK` into a
directory) or down (`Back`). This is the same shape `apps/settings` uses for
its confirm dialog — state lives in the app struct, not in a growing set of
views the dispatcher has to know about.

**The info dialog is a separate pushed view, not a `cads_dialog` nested inside
the browser's `draw()`.** The soft-key strip is only re-applied by the
compositor when the dispatcher's current view changes; a view has no supported
way to relabel its own keys while it stays current. Pushing the size dialog as
`CADS_VIEW_ID_FILEBROWSER_INFO` makes the *view change itself* carry the new
labels (`cads_dialog_softkeys()` into `cads_view_set_softkeys()` before the
push in `cads_filebrowser_show_info()`), so the physical strip and the OK
button drawn inside the box always agree — the same constraint `apps/settings`
documents for its own confirm dialog.

**Entries are not sorted.** littlefs returns directory entries in whatever
order its own metadata blocks hold them, not alphabetical. A browser this
small does not need to impose one; a future version that does can sort
`cads_filebrowser_t.entries` after `cads_filebrowser_refresh()` fills it.

**Mounting happens in `enter()`, not in `cads_filebrowser_init()`.** A volume
formatted after boot — by the settings screen's factory reset, or by the
explorer's storage test — is picked up the next time this app is opened rather
than needing a reboot. It never formats on its own: a browser that could wipe
the volume it is looking at would be a trap, not a convenience, and that stays
`apps/settings`' and the explorer's job.

**Fixed-size tables, no allocation.** `CADS_FILEBROWSER_MAX_ENTRIES` is 32:
`items`, `detail_text` and `entries` are all compile-time arrays inside the
static `s_browser`, matching `modules/storage`'s own no-heap policy. A
directory with more than 32 entries only shows the first 32 that
`cads_storage_dir_read()` returns before the loop stops.
`CADS_FILEBROWSER_DETAIL_MAX` is 15 — sized, per the comment in the source, so
that `"4294967295 B"` (the longest a `uint32_t` size can print) always fits
with room left over, never truncating a real size.

**`cads_filebrowser_go_down()` refuses rather than truncates.** If appending
the chosen entry's name would not fit in `CADS_STORAGE_PATH_MAX + 1` bytes, the
function returns `false` and leaves `app->path` untouched instead of silently
cutting the new path short and navigating somewhere the user did not select.

## How do I use it?

```c
#include "cads_filebrowser.h"
#include "cads_view_dispatcher.h"
#include "cads_softkeys.h"
#include "canvas.h"

static cads_view_entry_t entries[8];
static uint32_t nav_stack[4];
static cads_view_dispatcher_t dispatcher;

void app_open_filebrowser(void) {
    cads_view_dispatcher_init(&dispatcher, entries, 8u, nav_stack, 4u);

    const cads_rect_t content = {
        .x = 0,
        .y = 0,
        .width = CADS_CANVAS_WIDTH,
        .height = (int16_t)(CADS_CANVAS_HEIGHT - CADS_SOFTKEYS_HEIGHT),
    };
    cads_view_dispatcher_set_area(&dispatcher, content);

    cads_filebrowser_init(&dispatcher); /* called by apps/menu; not usually direct */

    cads_view_dispatcher_push(&dispatcher, CADS_VIEW_ID_FILEBROWSER);
}
```

In the real firmware this is `apps/menu/cads_menu_app.c`, which calls
`cads_filebrowser_init(dispatcher)` once at startup behind
`CADS_APP_FILEBROWSER_ENABLED` and pushes `CADS_VIEW_ID_FILEBROWSER` when the
user selects the file browser from the desktop.

## What are the limits?

- **No write operations.** No create, rename, delete or content viewer, even
  though the underlying `cads/storage/storage.h` supports all of them.
- **At most 32 entries per directory are shown**, and they are **not
  sorted** — whatever order littlefs' metadata blocks hold.
- **The info dialog shows a size in bytes only** — no timestamp, no
  permissions, nothing else `cads_storage_info_t` doesn't already carry (it
  carries only `name`, `size` and `type`).
- **Never formats or unmounts the volume.** If `cads_storage_mount()` fails on
  entry, the browser just shows an empty listing at `/`; recovering the volume
  is `apps/settings`' factory reset or the explorer's job, not this app's.
- **Single static instance.** `s_browser` and `s_info` are file-scope statics,
  not caller-allocated state, so there is exactly one file browser and one
  info dialog in the firmware image.
