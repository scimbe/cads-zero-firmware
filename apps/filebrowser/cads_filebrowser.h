/*
 * CaDS Zero - file browser: read-only navigation of the littlefs volume.
 */

#ifndef CADS_FILEBROWSER_H
#define CADS_FILEBROWSER_H

#include "cads_view_dispatcher.h"

#define CADS_VIEW_ID_FILEBROWSER      0x0700u
/** A file's size, pushed as its own view - see cads_settings.c for why a
 *  dialog nested inside another view's draw path is the wrong shape here. */
#define CADS_VIEW_ID_FILEBROWSER_INFO 0x0701u

/** Register both file browser views with the dispatcher. */
void cads_filebrowser_init(cads_view_dispatcher_t* dispatcher);

#endif /* CADS_FILEBROWSER_H */
