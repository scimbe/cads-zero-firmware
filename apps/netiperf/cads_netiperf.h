/*
 * CaDS Zero - iperf server and client, as GUI apps.
 *
 * Thin glue over lwIP's own lwiperf app (lib/lwip/src/apps/lwiperf), the
 * same third-party throughput tester apps/bringup/explorer_iperf_demo.c
 * already wires in as an explorer command. This is the same measurement
 * logic, reachable from the app tree instead of only the serial console,
 * with both directions: a server (what the explorer command already did)
 * and a client (new - explorer_iperf_demo.c never called
 * lwiperf_start_tcp_client_default(), only the server half).
 */

#ifndef CADS_NETIPERF_H
#define CADS_NETIPERF_H

#include "cads_view_dispatcher.h"

#define CADS_VIEW_ID_IPERF_SERVER 0x0900u
#define CADS_VIEW_ID_IPERF_CLIENT 0x0901u

void cads_netiperf_init(cads_view_dispatcher_t* dispatcher);

#endif /* CADS_NETIPERF_H */
