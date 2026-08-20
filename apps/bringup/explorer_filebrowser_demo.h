#ifndef CADS_EXPLORER_FILEBROWSER_DEMO_H
#define CADS_EXPLORER_FILEBROWSER_DEMO_H

#include <stdint.h>

/**
 * apps/filebrowser live on the panel for `seconds`, chrome enabled (status
 * bar shows the current path, soft-key strip shows Up/Down/Open/Back) -
 * reached directly rather than through desktop -> menu, the same shortcut
 * explorer_gui_demo.c takes for apps/gpio, for the same reason: proving the
 * app renders and takes input on real silicon does not need the rest of the
 * tree in the way.
 */
void cads_explorer_filebrowser_demo(uint32_t seconds);

#endif /* CADS_EXPLORER_FILEBROWSER_DEMO_H */
