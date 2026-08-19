/*
 * A drop-in test double for cads_hal_eth_mdio_read(), scripted per register -
 * same discipline as fake_hal.h: reset from setUp() so no test can be
 * influenced by the one before it.
 *
 * It implements cads_hal_eth_mdio_read() from hal_eth_mdio.h and nothing
 * else from that header. That is deliberate: hal_eth_aneg.c and
 * hal_eth_linklog.c only ever read registers, never write or probe identity,
 * so a fake cads_hal_eth_mdio_write()/cads_hal_eth_mdio_init() would be dead
 * code that link only pulls in if some future subject under test actually
 * calls it - at which point it belongs here, not before.
 */

#ifndef CADS_FAKE_MDIO_H
#define CADS_FAKE_MDIO_H

#include <stdbool.h>
#include <stdint.h>

/** Forget every scripted register and read count. */
void cads_fake_mdio_reset(void);

/** Program register `reg` on PHY `phy` to answer `value` on every read from
 *  now on, until reprogrammed or reset. A register never set answers false
 *  ("no PHY there"), same as the real driver on a silent bus. */
void cads_fake_mdio_set(uint8_t phy, uint8_t reg, uint16_t value);

/** Queue register `reg` on PHY `phy` to answer `value` exactly once, then
 *  fall back to whatever cads_fake_mdio_set() left standing (or "no PHY
 *  there" if nothing was set). Models a latch-on-read register where the
 *  first poll finds an event set and the next finds it already clear. */
void cads_fake_mdio_queue_once(uint8_t phy, uint8_t reg, uint16_t value);

/** How many times cads_hal_eth_mdio_read() was called for (phy, reg) since
 *  the last reset - lets a test assert a module reads what it needs and
 *  nothing it does not. */
uint32_t cads_fake_mdio_read_count(uint8_t phy, uint8_t reg);

#endif /* CADS_FAKE_MDIO_H */
