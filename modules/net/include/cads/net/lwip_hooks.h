/*
 * CaDS Zero - lwIP hook declarations (LWIP_HOOK_FILENAME, lwipopts.h).
 *
 * Included by lwIP's own sources after their headers, so lwIP types are
 * available here - unlike in lwipopts.h itself.
 */

#ifndef CADS_NET_LWIP_HOOKS_H
#define CADS_NET_LWIP_HOOKS_H

#include "lwip/ip_addr.h"

/** LWIP_HOOK_TCP_ISN: RFC 6528 initial sequence number (cads/net/rand.h). */
u32_t cads_lwip_tcp_isn(const ip_addr_t* local_ip, u16_t local_port, const ip_addr_t* remote_ip, u16_t remote_port);

#endif /* CADS_NET_LWIP_HOOKS_H */
