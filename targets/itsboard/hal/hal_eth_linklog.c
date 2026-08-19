/*
 * CaDS Zero - link event log for the LAN8742A, polled rather than
 * interrupt-driven.
 *
 * Register 29 (Interrupt Source Flag) latches high on the events below and
 * clears itself on read - documented LAN8742A behaviour, not inferred - which
 * is exactly what a poll loop needs and nothing more: no NVIC/EXTI wiring, no
 * state shared with an ISR, no critical section anywhere in this file.
 */

#include "hal_eth_linklog.h"

#include "cads_hal.h"
#include "hal_eth_mdio.h"

#define PHY_ISF 29u /* Interrupt Source Flag Register, latch-high/clear-on-read */

#define ISF_LINK_DOWN_IT          (1u << 4) /* INT4 */
#define ISF_REMOTE_FAULT_IT       (1u << 5) /* INT5 */
#define ISF_AUTONEGO_COMPLETE_IT  (1u << 6) /* INT6 */

void cads_eth_linklog_init(cads_eth_linklog_t* log) {
    log->head = 0u;
    log->count = 0u;
    log->dropped = 0u;
}

static void cads_eth_linklog_push(
    cads_eth_linklog_t* log, uint32_t timestamp_ms, cads_eth_link_event_type_t type) {
    log->entries[log->head].timestamp_ms = timestamp_ms;
    log->entries[log->head].type = type;
    log->head = (log->head + 1u) % CADS_ETH_LINKLOG_CAPACITY;

    if(log->count < CADS_ETH_LINKLOG_CAPACITY) {
        log->count++;
    } else {
        /* The slot just overwritten held the oldest surviving entry. */
        log->dropped++;
    }
}

bool cads_hal_eth_linklog_poll(uint8_t phy, cads_eth_linklog_t* log) {
    uint16_t isf;
    if(!cads_hal_eth_mdio_read(phy, PHY_ISF, &isf)) return false;
    if(isf == 0u) return true; /* nothing latched, which is not an error */

    uint32_t now = cads_hal_ticks_ms();

    /* Link-down first when several events latch between two polls: it is
     * usually the one that explains the others (auto-negotiation restarting
     * after a cable is reseated typically re-latches AN complete too). */
    if(isf & ISF_LINK_DOWN_IT) cads_eth_linklog_push(log, now, CadsEthLinkEventDown);
    if(isf & ISF_REMOTE_FAULT_IT) cads_eth_linklog_push(log, now, CadsEthLinkEventRemoteFault);
    if(isf & ISF_AUTONEGO_COMPLETE_IT) cads_eth_linklog_push(log, now, CadsEthLinkEventAnegComplete);

    return true;
}

uint32_t cads_eth_linklog_count(const cads_eth_linklog_t* log) {
    return log->count;
}

const cads_eth_link_event_t* cads_eth_linklog_at(const cads_eth_linklog_t* log, uint32_t index) {
    if(index >= log->count) return NULL;

    uint32_t oldest = (log->head + CADS_ETH_LINKLOG_CAPACITY - log->count) % CADS_ETH_LINKLOG_CAPACITY;
    return &log->entries[(oldest + index) % CADS_ETH_LINKLOG_CAPACITY];
}
