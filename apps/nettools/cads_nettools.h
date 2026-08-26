/*
 * CaDS Zero - network tools, as a GUI menu section.
 *
 * The app-tree face of docs/ROADMAP.md's "network Swiss-army-knife": the
 * interactive diagnostics that already existed as explorer console commands
 * (ping `P`, ARP scan `A`, traceroute `T`), reachable from the panel with
 * buttons/touch instead of only over serial. One submenu view groups them
 * together with the network apps that already had views of their own
 * (Network Info, iperf server/client) - the menu section the tools belong
 * to, not another flat top-level row per tool.
 *
 * Everything here goes through modules/net's portable API
 * (cads_net_ping/arp_probe/traceroute_probe), which has honest simulator
 * implementations - so this app is fully portable and needs no board/sim
 * source split of its own.
 */

#ifndef CADS_NETTOOLS_H
#define CADS_NETTOOLS_H

#include "cads_view_dispatcher.h"

#define CADS_VIEW_ID_NETTOOLS       0x0A00u /**< the "Network" submenu     */
#define CADS_VIEW_ID_NETTOOLS_PING  0x0A01u
#define CADS_VIEW_ID_NETTOOLS_ARP   0x0A02u
#define CADS_VIEW_ID_NETTOOLS_TRACE 0x0A03u

void cads_nettools_init(cads_view_dispatcher_t* dispatcher);

#endif /* CADS_NETTOOLS_H */
