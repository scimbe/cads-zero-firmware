/*
 * CaDS Zero - cads/net's board-only half: the RMII data path.
 *
 * Read docs/SAFETY.md section 6 and docs/explanation/pa7-conflict.md first.
 *
 * DESCRIPTOR FORMAT. Chained mode (RDES1.RCH / TDES0.TCH), not ring mode:
 * every descriptor's word 3 is an explicit pointer to the next one, the last
 * one's pointing back to the first, rather than relying on an RER/TER
 * end-of-list bit and implicit array contiguity. Normal (16-byte) descriptors,
 * not enhanced (32-byte) - this driver does not use IEEE1588 timestamping or
 * hardware IPv4 checksum offload, and RM0090 requires enhanced descriptors
 * for either. Every bit position below was checked against RM0090 (archived
 * at docs/reference/datasheets/RM0090-stm32f4-reference-manual.pdf,
 * "Ethernet (ETH): media access control (MAC) with DMA controller", the
 * "Normal Rx/Tx DMA descriptors" figures) rather than written from memory -
 * RDES1's RER turned out to be bit 15, not the bit 14 a first guess landed
 * on, which is exactly the kind of one-bit-off mistake that would have
 * produced a driver that mostly worked and occasionally corrupted the
 * descriptor ring.
 *
 * WHY NOT .ramfunc. modules/storage's flash driver tried exactly this
 * pattern first and found real, intermittent BusFaults running from RAM
 * where the identical operation from flash was reliable every time (see
 * modules/storage/src/cads_flash_stm32f4.c's file header and
 * docs/SAFETY.md section 4). Nothing here has been tested from RAM and
 * there is no reason to invite the same class of bug a second time; this
 * driver runs from flash throughout, which the dual-bank part supports
 * without qualification for MAC/DMA register access (unlike the flash
 * controller case, this is not even sharing a memory bank with anything).
 *
 * WHY NOT HARDWARE CHECKSUM OFFLOAD. It would save CPU cycles the MDIO-only
 * diagnostics already measured are not the bottleneck (docs/ROADMAP.md M5:
 * the display bus is 97% saturated, not the CPU), needs enhanced
 * descriptors, and lwIP computes checksums in software by default with no
 * driver-side work at all. Not worth the added surface area for a first
 * working version.
 *
 * BUFFERS ARE COPIED, NOT ZERO-COPY. cads_hal_eth_mac_transmit() copies the
 * caller's data into a driver-owned buffer already wired into a descriptor;
 * cads_hal_eth_mac_receive() copies out of one. A zero-copy design (handing
 * lwIP pbufs the DMA buffers directly) saves the copy but means pbuf
 * lifetime and descriptor ownership have to agree, which is a second thing
 * to get right at the same time as the descriptor ring itself. Copying is
 * the standard, easy-to-reason-about choice for a first driver; nothing
 * about the API here would need to change to switch later.
 */

#include "hal_eth_mac.h"

#include <string.h>

#include "board.h"
#include "cads_hal.h"
#include "hal_gpio.h"

#define CADS_ETH_RX_COUNT 4u
#define CADS_ETH_TX_COUNT 4u
#define CADS_ETH_BUF_SIZE 1536u /* > 1518 (max untagged frame incl. CRC), multiple of 4 */

/* Normal (non-enhanced) descriptor: four 32-bit words, RM0090 Figures 379/382. */
typedef struct {
    volatile uint32_t status;  /* RDES0 / TDES0 */
    volatile uint32_t control; /* RDES1 / TDES1 */
    volatile uint32_t buffer1; /* RDES2 / TDES2 - the data buffer address    */
    volatile uint32_t next;    /* RDES3 / TDES3 - next descriptor (chained) */
} cads_eth_desc_t;

/* RDES0 */
#define CADS_ETH_RDES0_OWN     (1u << 31)
#define CADS_ETH_RDES0_FL_Pos  16u
#define CADS_ETH_RDES0_FL_Msk  (0x3FFFu << CADS_ETH_RDES0_FL_Pos)
#define CADS_ETH_RDES0_ES      (1u << 15)
#define CADS_ETH_RDES0_FS      (1u << 9)
#define CADS_ETH_RDES0_LS      (1u << 8)
/* RDES1 */
#define CADS_ETH_RDES1_RCH     (1u << 14)
/* TDES0 */
#define CADS_ETH_TDES0_OWN     (1u << 31)
#define CADS_ETH_TDES0_LS      (1u << 29)
#define CADS_ETH_TDES0_FS      (1u << 28)
#define CADS_ETH_TDES0_TCH     (1u << 20)
#define CADS_ETH_TDES0_ES      (1u << 15)

/* Descriptors and buffers: ordinary .bss, which this linker script places in
 * RAM (never CCM - see targets/itsboard/linker/cads_itsboard.ld), so they
 * are DMA-reachable without any special section attribute. */
static cads_eth_desc_t cads_eth_rx_desc[CADS_ETH_RX_COUNT];
static cads_eth_desc_t cads_eth_tx_desc[CADS_ETH_TX_COUNT];
static uint8_t cads_eth_rx_buf[CADS_ETH_RX_COUNT][CADS_ETH_BUF_SIZE];
static uint8_t cads_eth_tx_buf[CADS_ETH_TX_COUNT][CADS_ETH_BUF_SIZE];

static uint32_t cads_eth_rx_next = 0u; /* next descriptor to poll for a received frame */
static uint32_t cads_eth_tx_next = 0u; /* next descriptor to use for a new transmit    */

static void cads_eth_rmii_pins_init(void) {
    /* PA7 (CRS_DV) is deliberately absent here: hal_spi.c's
     * cads_hal_spi_claim_bus()/release_bus() owns that pin's alternate
     * function once cads_hal_spi_set_eth_datapath_active() is in play, per
     * this file's own header comment. */
    cads_gpio_init_alternate(
        CADS_PIN_ETH_REF_CLK_PORT, CADS_PIN_ETH_REF_CLK, CADS_ETH_AF, CadsGpioPullNone);
    cads_gpio_init_alternate(
        CADS_PIN_ETH_RXD0_PORT, CADS_PIN_ETH_RXD0, CADS_ETH_AF, CadsGpioPullNone);
    cads_gpio_init_alternate(
        CADS_PIN_ETH_RXD1_PORT, CADS_PIN_ETH_RXD1, CADS_ETH_AF, CadsGpioPullNone);
    cads_gpio_init_alternate(
        CADS_PIN_ETH_RXER_PORT, CADS_PIN_ETH_RXER, CADS_ETH_AF, CadsGpioPullNone);
    cads_gpio_init_alternate(
        CADS_PIN_ETH_TX_EN_PORT, CADS_PIN_ETH_TX_EN, CADS_ETH_AF, CadsGpioPullNone);
    cads_gpio_init_alternate(
        CADS_PIN_ETH_TXD0_PORT, CADS_PIN_ETH_TXD0, CADS_ETH_AF, CadsGpioPullNone);
    cads_gpio_init_alternate(
        CADS_PIN_ETH_TXD1_PORT, CADS_PIN_ETH_TXD1, CADS_ETH_AF, CadsGpioPullNone);
}

static void cads_eth_desc_rings_init(void) {
    for(uint32_t i = 0; i < CADS_ETH_RX_COUNT; i++) {
        cads_eth_desc_t* next = &cads_eth_rx_desc[(i + 1u) % CADS_ETH_RX_COUNT];
        cads_eth_rx_desc[i].buffer1 = (uint32_t)cads_eth_rx_buf[i];
        cads_eth_rx_desc[i].control = CADS_ETH_RDES1_RCH | CADS_ETH_BUF_SIZE;
        cads_eth_rx_desc[i].next = (uint32_t)next;
        cads_eth_rx_desc[i].status = CADS_ETH_RDES0_OWN; /* ready for the DMA from the start */
    }
    for(uint32_t i = 0; i < CADS_ETH_TX_COUNT; i++) {
        cads_eth_desc_t* next = &cads_eth_tx_desc[(i + 1u) % CADS_ETH_TX_COUNT];
        cads_eth_tx_desc[i].buffer1 = (uint32_t)cads_eth_tx_buf[i];
        cads_eth_tx_desc[i].control = 0u;
        cads_eth_tx_desc[i].next = (uint32_t)next;
        cads_eth_tx_desc[i].status = 0u; /* owned by us until there is something to send */
    }
    cads_eth_rx_next = 0u;
    cads_eth_tx_next = 0u;
}

void cads_hal_eth_mac_init(const uint8_t mac_address[6], bool full_duplex, bool speed_100) {
    cads_eth_rmii_pins_init();

    /* MAC address: MACA0HR bits [15:0] hold bytes 5:4 (big end of the
     * address); MACA0LR holds bytes 3:0. MACA0HR bit 31 (MO) always reads 1
     * for address 0 and cannot be cleared - RM0090 table under
     * ETH_MACA0HR: "always 1". */
    uint32_t hi = ((uint32_t)mac_address[5] << 8) | mac_address[4];
    uint32_t lo = ((uint32_t)mac_address[3] << 24) | ((uint32_t)mac_address[2] << 16) |
                  ((uint32_t)mac_address[1] << 8) | mac_address[0];
    ETH->MACA0HR = hi;
    ETH->MACA0LR = lo;

    uint32_t maccr = ETH->MACCR;
    maccr &= ~(ETH_MACCR_DM | ETH_MACCR_FES | ETH_MACCR_LM);
    if(full_duplex) maccr |= ETH_MACCR_DM;
    if(speed_100) maccr |= ETH_MACCR_FES;
    maccr |= ETH_MACCR_APCS; /* strip pad/FCS on receive - lwIP wants the payload, not padding */
    ETH->MACCR = maccr;

    /* Software reset: DMABMR.SR, self-clearing. RM0090 requires reading it
     * as 0 before touching any other register of the core. */
    ETH->DMABMR |= ETH_DMABMR_SR;
    uint32_t guard = 100000u;
    while((ETH->DMABMR & ETH_DMABMR_SR) && guard--) {
    }

    cads_eth_desc_rings_init();

    ETH->DMARDLAR = (uint32_t)&cads_eth_rx_desc[0];
    ETH->DMATDLAR = (uint32_t)&cads_eth_tx_desc[0];

    /* Store-and-forward on both directions: the DMA waits for a complete
     * frame in the FIFO before moving it, trading a little latency for
     * never handing lwIP a frame it has to discard because the FIFO
     * underran mid-transfer. */
    ETH->DMAOMR |= ETH_DMAOMR_RSF | ETH_DMAOMR_TSF;

    /* Clear any status left over from a previous run (or the reset above)
     * before enabling interrupts-as-status-only polling. */
    ETH->DMASR = ETH->DMASR;
}

void cads_hal_eth_mac_start(void) {
    ETH->DMAOMR |= ETH_DMAOMR_ST | ETH_DMAOMR_SR;
    ETH->MACCR |= ETH_MACCR_TE | ETH_MACCR_RE;
}

void cads_hal_eth_mac_stop(void) {
    ETH->MACCR &= ~(ETH_MACCR_TE | ETH_MACCR_RE);
    ETH->DMAOMR &= ~(ETH_DMAOMR_ST | ETH_DMAOMR_SR);
}

bool cads_hal_eth_mac_transmit(const uint8_t* data, uint16_t length) {
    if(!data || length == 0u || length > CADS_ETH_BUF_SIZE) return false;

    cads_eth_desc_t* desc = &cads_eth_tx_desc[cads_eth_tx_next];
    if(desc->status & CADS_ETH_TDES0_OWN) return false; /* ring full - DMA still has this one */

    memcpy((void*)desc->buffer1, data, length);
    desc->control = length; /* TBS1, bits 12:0 - length is always < CADS_ETH_BUF_SIZE (8191 max) */
    desc->status = CADS_ETH_TDES0_TCH | CADS_ETH_TDES0_FS | CADS_ETH_TDES0_LS | CADS_ETH_TDES0_OWN;

    ETH->DMATPDR = 0u; /* poll demand: wake the TxDMA if it was suspended */

    cads_eth_tx_next = (cads_eth_tx_next + 1u) % CADS_ETH_TX_COUNT;
    return true;
}

uint16_t cads_hal_eth_mac_receive(uint8_t* buffer, uint16_t buffer_size) {
    cads_eth_desc_t* desc = &cads_eth_rx_desc[cads_eth_rx_next];
    if(desc->status & CADS_ETH_RDES0_OWN) return 0u; /* still the DMA's - nothing waiting */

    uint16_t result = 0u;

    /* FS+LS both set is the only shape this driver hands to lwIP: a whole
     * frame in one buffer. CADS_ETH_BUF_SIZE (1536) exceeds the largest
     * frame this MAC can receive with APCS stripping pad/FCS, so a frame
     * spanning multiple descriptors would mean something is misconfigured
     * upstream - dropped, not stitched back together, same as any other
     * frame this driver does not recognise as complete and well-formed. */
    bool complete = (desc->status & (CADS_ETH_RDES0_FS | CADS_ETH_RDES0_LS)) ==
                     (CADS_ETH_RDES0_FS | CADS_ETH_RDES0_LS);
    bool ok = complete && !(desc->status & CADS_ETH_RDES0_ES);

    if(ok) {
        uint32_t length = (desc->status & CADS_ETH_RDES0_FL_Msk) >> CADS_ETH_RDES0_FL_Pos;
        if(length <= buffer_size) {
            memcpy(buffer, (const void*)desc->buffer1, length);
            result = (uint16_t)length;
        }
        /* length > buffer_size: dropped, result stays 0 - see the header's
         * "counted, not truncated" contract. */
    }

    desc->control = CADS_ETH_RDES1_RCH | CADS_ETH_BUF_SIZE;
    desc->status = CADS_ETH_RDES0_OWN; /* hand this descriptor back to the DMA */
    ETH->DMARPDR = 0u; /* poll demand: wake the RxDMA if it was suspended */

    cads_eth_rx_next = (cads_eth_rx_next + 1u) % CADS_ETH_RX_COUNT;
    return result;
}
