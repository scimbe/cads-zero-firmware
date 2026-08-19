#ifndef CADS_HAL_ETH_LINKLOG_H
#define CADS_HAL_ETH_LINKLOG_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Link event log for the LAN8742A, built by polling rather than interrupts.
 *
 * Register 29 (Interrupt Source Flag) latches high on Link Down, Remote
 * Fault and Auto-Negotiation Complete and clears itself on read - exactly
 * what a poll loop needs, with no NVIC/EXTI wiring and no state shared with
 * an ISR. The cost is resolution: an event between two polls is still
 * caught, because the latch survives until it is read, but two events of the
 * same kind between polls collapse into a single log entry, and the
 * timestamp records when this code noticed, not when the PHY did.
 *
 * The log is a fixed-size ring, statically allocated in the caller-owned
 * struct - no malloc anywhere in this project, see
 * docs/reference/module-layout.md.
 */

typedef enum {
    CadsEthLinkEventDown = 0,     /**< INT4: link transitioned down */
    CadsEthLinkEventAnegComplete, /**< INT6: auto-negotiation finished */
    CadsEthLinkEventRemoteFault,  /**< INT5: partner signalled a fault */
} cads_eth_link_event_type_t;

typedef struct {
    uint32_t timestamp_ms;     /**< cads_hal_ticks_ms() at the poll that
                                 *   observed the event, not when the PHY
                                 *   latched it */
    cads_eth_link_event_type_t type;
} cads_eth_link_event_t;

#define CADS_ETH_LINKLOG_CAPACITY 32u

typedef struct {
    cads_eth_link_event_t entries[CADS_ETH_LINKLOG_CAPACITY];
    uint32_t head;    /**< next write position */
    uint32_t count;   /**< entries currently valid, capped at capacity */
    uint32_t dropped; /**< events overwritten because the log was full */
} cads_eth_linklog_t;

/** Discard everything logged, including the drop count. */
void cads_eth_linklog_init(cads_eth_linklog_t* log);

/**
 * Poll Register 29 once and append any events it reports. Safe to call at
 * any rate; a call that finds nothing latched appends nothing. Returns false
 * only when the MDIO read itself failed - false does not mean "no events",
 * it means the register could not be read at all.
 */
bool cads_hal_eth_linklog_poll(uint8_t phy, cads_eth_linklog_t* log);

uint32_t cads_eth_linklog_count(const cads_eth_linklog_t* log);

/** The `index`th oldest entry still held (0 is the oldest), or NULL once
 *  `index` reaches cads_eth_linklog_count(). */
const cads_eth_link_event_t* cads_eth_linklog_at(const cads_eth_linklog_t* log, uint32_t index);

#endif /* CADS_HAL_ETH_LINKLOG_H */
