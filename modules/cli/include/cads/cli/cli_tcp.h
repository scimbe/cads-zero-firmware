/*
 * CaDS Zero - cads_cli reachable over TCP.
 *
 * Board only - the simulator has no network stack at all (see
 * modules/net/src/cads_net_sim.c's own file header), so
 * cads_cli_tcp_sim.c's cads_cli_tcp_start() honestly reports that instead
 * of pretending a socket exists.
 */

#ifndef CADS_CLI_TCP_H
#define CADS_CLI_TCP_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start listening for cads_cli connections on `port`.
 *
 * Requires cads_net_init() to already have brought the netif up - this
 * only adds a listening PCB to it. Idempotent: a second call while a
 * listener already exists is a no-op that returns true.
 *
 * One connection at a time. A second concurrent connection attempt is
 * accepted at the TCP level and then immediately closed - this is a
 * diagnostic tool for one operator, not a multi-user shell, and letting two
 * sessions dispatch into the same command table's side effects
 * unsynchronised would be a real bug, not a feature.
 *
 * Returns false if the listening socket could not be created or bound
 * (out of PCBs, or the port is already taken by something else).
 */
bool cads_cli_tcp_start(uint16_t port);

/**
 * Run the commands the TCP session has received since the last call.
 *
 * The connection's tcp_recv callback only queues bytes: it fires inside
 * cads_net_poll(), and a command that pumps the network itself (a ping, an
 * ARP probe) would re-enter lwIP's tcp_input() from there. Call this from
 * the loop that calls cads_net_poll() - after it, never from inside an lwIP
 * callback. Commands run here may call cads_net_poll() freely, and their
 * output may wait (bounded) for the peer's ACKs instead of being truncated.
 * Cheap when nothing is queued; a no-op on the simulator.
 */
void cads_cli_tcp_service(void);

#ifdef __cplusplus
}
#endif

#endif /* CADS_CLI_TCP_H */
