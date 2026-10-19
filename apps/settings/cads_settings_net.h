/*
 * CaDS Zero - Settings -> Network: change the IP address, netmask, gateway and
 * DHCP on the panel, without a host and without editing /config.txt.
 *
 * Two views. The list (DHCP, IP address, Netmask, Gateway, Save & apply) edits
 * a working copy; the octet editor changes one address in that copy (Up/Down
 * change the octet, Left/Right pick it, Set keeps it). "Save & apply" is only a
 * REQUEST: cads/storage is not thread-safe and the list runs on the input task,
 * so cads_settings.c takes the request on the console task
 * (cads_settings_net_take_save), writes /config.txt and applies the network live
 * - the same split as Reload config and Factory reset.
 */

#ifndef CADS_SETTINGS_NET_H
#define CADS_SETTINGS_NET_H

#include <stdbool.h>

#include "cads/config/config.h"
#include "cads_view_dispatcher.h"

#define CADS_VIEW_ID_SETTINGS_NET      0x0304u /**< the list: DHCP / IP / Netmask / Gateway / Save */
#define CADS_VIEW_ID_SETTINGS_NET_EDIT 0x0305u /**< the octet editor for one address */

/** Register both views. */
void cads_settings_net_init(cads_view_dispatcher_t* dispatcher);

/** Start from the network settings currently applied in `current` and show the
 *  list. Anything edited but not saved is dropped. */
void cads_settings_net_open(const cads_config_t* current);

/**
 * Console task: true once after the user chose "Save & apply" with a usable
 * configuration. `out`'s net_dhcp / net_ip / net_netmask / net_gateway are the
 * working copy; the caller loads the rest of the config, overlays these four,
 * saves and applies.
 */
bool cads_settings_net_take_save(cads_config_t* out);

/** Console task: report the outcome of that save on the list's status line. */
void cads_settings_net_saved(bool ok);

#endif /* CADS_SETTINGS_NET_H */
