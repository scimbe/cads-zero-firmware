/*
 * CaDS Zero - network info: link state and PHY information.
 */

#ifndef CADS_NETINFO_H
#define CADS_NETINFO_H

#include "cads_view_dispatcher.h"

#define CADS_VIEW_ID_NETINFO 0x0600u

void cads_netinfo_init(cads_view_dispatcher_t* dispatcher);

#endif /* CADS_NETINFO_H */
