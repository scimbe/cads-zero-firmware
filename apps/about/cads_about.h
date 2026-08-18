/*
 * CaDS Zero - about: the board descriptor, read and displayed rather than
 * assumed.
 */

#ifndef CADS_ABOUT_H
#define CADS_ABOUT_H

#include "cads_view_dispatcher.h"

#define CADS_VIEW_ID_ABOUT 0x0400u

void cads_about_init(cads_view_dispatcher_t* dispatcher);

#endif /* CADS_ABOUT_H */
