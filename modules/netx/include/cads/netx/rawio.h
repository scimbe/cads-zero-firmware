/*
 * CaDS Zero - active tooling raw L2 I/O + capture-session surface (M9).
 *
 * The board/sim split of modules/netx: the half that talks to hardware
 * (or, on the host, honestly does not). frame.h is the portable half and
 * is unit-tested on host; this half is exercised only on the board -
 * the same division modules/net and modules/storage already use.
 *
 * WHY A CAPTURE SESSION EXISTS
 * ----------------------------
 * Two of the seven tools (#3 802.1X MAC cloning, #5 TCP RST daemon)
 * must observe traffic other hosts generate, not just traffic addressed
 * to this board. lwIP's raw PCBs only see packets addressed to this
 * netif's MAC; EAPOL and other-host TCP never arrive that way. Those
 * tools therefore take the MAC promiscuous and drain the RX ring
 * themselves for the duration of a session - which is incompatible with
 * cads_net_poll() draining the same ring, so a session also suppresses
 * poll (see cads_net_set_poll_suppressed). begin/end is a pair so the
 * cleanup is guaranteed symmetric: every session that turns promiscuous
 * on and poll off also turns them back off and on, including on view
 * exit (apps/active wires the exit callback to _capture_end).
 *
 * The TX-only tools (#1, #4, #7) and the lwIP-RX tools (#2, #6) do not
 * take a capture session - they call cads_netx_tx_raw for sends and let
 * cads_net_poll keep running (or use raw PCBs). Only the two promiscuous
 * tools call begin/end.
 *
 * HOST (sim) BEHAVIOUR
 * --------------------
 * There is no RMII in the simulator and no RX ring to own, so every
 * function here is an honest no-op/false on host: tx returns false,
 * capture_begin returns false, capture_drain returns 0. This lets the
 * suite app build and the host gallery exercise its UI navigation
 * without pretending any packets moved - the same "no link, ever"
 * contract modules/net/src/cads_net_sim.c already upholds.
 */

#ifndef CADS_NETX_RAWIO_H
#define CADS_NETX_RAWIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Transmit one raw Ethernet frame (14-byte header + payload, no FCS). On
 * the board this calls cads_hal_eth_mac_transmit and so inherits its
 * serialized-with-the-SPI-mutex contract (PA7 time-slicing, SAFETY.md §6)
 * - callers must not be in a path that already holds the SPI bus. On the
 * host this is a no-op that returns false.
 *
 * Returns true if the frame was queued for transmit, false on the host or
 * if the MAC driver rejected it (no link, TX ring full).
 */
bool cads_netx_tx_raw(const uint8_t* frame, uint16_t len);

/**
 * Take the MAC promiscuous and suppress cads_net_poll() for the duration
 * of a capture session. Only one session may be active at a time; a
 * second begin returns false (the suite's single-session-owner pattern
 * in apps/active enforces this at the UI level, but the raw layer is the
 * real guard - a tool that forgets to end a session cannot start a
 * second one and wedge the ring).
 *
 * Returns true on the board when the session was started (or was already
 * active - idempotent begin), false on the host or if the MAC could not
 * be set promiscuous.
 */
bool cads_netx_capture_begin(void);

/**
 * End a capture session: MAC back to its normal address-filtered mode,
 * cads_net_poll() resumed. Safe to call when no session is active (a
 * no-op in that case) so a view's exit callback can call it
 * unconditionally. No-op on the host.
 */
void cads_netx_capture_end(void);

/**
 * Drain at most one captured frame from the RX ring into `buf` (size
 * `cap`), non-blocking. Returns the frame length, or 0 when the ring is
 * empty or the frame was larger than `cap` (dropped). 0 on the host.
 *
 * Call this from a promiscuous tool's _tick; the budget is the caller's
 * (drain until empty, or until a small per-tick cap, depending on the
 * tool). Frames larger than `cap` are dropped rather than truncated:
 * a tool that cares about the full frame must size its buffer to
 * CADS_NETX_FRAME_MAX.
 */
uint16_t cads_netx_capture_drain(uint8_t* buf, uint16_t cap);

#ifdef __cplusplus
}
#endif

#endif /* CADS_NETX_RAWIO_H */