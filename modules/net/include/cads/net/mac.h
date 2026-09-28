/*
 * CaDS Zero - the board's default Ethernet address.
 *
 * Every board used to boot as 02:CA:D5:5E:00:01. Harmless on one
 * point-to-point cable, a collision the moment two boards share a switch (a
 * lab room, a demo). The default is now derived from the STM32's 96-bit
 * unique device ID: stable across resets and reflashes of one board,
 * different between boards. Portable (host tested, tests/unit/test_net_mac.c);
 * reading the UID itself is the board's job (apps/bringup/explorer_eth.c).
 */

#ifndef CADS_NET_MAC_H
#define CADS_NET_MAC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 02:CA:D5 followed by a 24-bit hash of `uid` (the three UID words, RM0090
 * 39.1). 0x02 = locally administered (bit 1) unicast (bit 0 clear), per IEEE
 * 802 - the address can never collide with a vendor-assigned one. 24 bits
 * make a collision between two given boards a 1 in 16.7 million event.
 */
void cads_net_mac_from_uid(const uint32_t uid[3], uint8_t mac[6]);

#ifdef __cplusplus
}
#endif

#endif /* CADS_NET_MAC_H */
