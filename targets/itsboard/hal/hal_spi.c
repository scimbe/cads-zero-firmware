/*
 * CaDS Zero - SPI1 bus shared by the ILI9486 panel and the XPT2046 touch
 * controller, plus the arbitration against the Ethernet MAC.
 *
 * THE PA7 PROBLEM
 * ---------------
 * On a stock board Arduino D11 (SPI1_MOSI) and ETH_RMII_CRS_DV are the same
 * physical pin, PA7, and only one alternate function can own it at a time.
 *
 * The ITS lab firmware solves this per BYTE: stop the MAC, flip the AF, send
 * one byte, flip back, restart the MAC. That is correct but ruinous - it makes
 * DMA impossible and tears down the receiver 150 000 times for a full screen.
 *
 * We claim the bus per BLIT instead. One stop/flip, one DMA transfer of an
 * entire rectangle, one flip/restart. Same correctness, three orders of
 * magnitude fewer MAC restarts.
 *
 * With SB121/SB122 swapped (CADS_SPI_MOSI_ON_PB5=1) MOSI moves to PB5, PA7
 * belongs to the PHY alone, and all of this arbitration compiles away.
 */

#include "hal_spi.h"

#include "board.h"
#include "cads_hal.h"
#include "hal_gpio.h"

/* SPI1_TX is DMA2, stream 3, channel 3 (RM0090 table 43). */
#define CADS_SPI_DMA        DMA2
#define CADS_SPI_DMA_STREAM DMA2_Stream3
#define CADS_SPI_DMA_CHANNEL 3u

static volatile bool cads_spi_dma_active = false;

static uint32_t cads_spi_br_bits(uint32_t divider) {
    switch(divider) {
    case 2u: return 0u << SPI_CR1_BR_Pos;
    case 4u: return 1u << SPI_CR1_BR_Pos;
    case 8u: return 2u << SPI_CR1_BR_Pos;
    case 16u: return 3u << SPI_CR1_BR_Pos;
    case 32u: return 4u << SPI_CR1_BR_Pos;
    case 64u: return 5u << SPI_CR1_BR_Pos;
    case 128u: return 6u << SPI_CR1_BR_Pos;
    default: return 7u << SPI_CR1_BR_Pos; /* /256 */
    }
}

static void cads_spi_configure(uint32_t divider, bool sixteen_bit) {
    CADS_LCD_SPI->CR1 &= ~SPI_CR1_SPE;

    /* Mode 0 (CPOL=0, CPHA=0), MSB first, master, software NSS.
     * These are the settings the panel glue logic has always been driven
     * with; the shift register chain latches on the rising edge. */
    CADS_LCD_SPI->CR1 = SPI_CR1_SSM | SPI_CR1_SSI | SPI_CR1_MSTR |
                        cads_spi_br_bits(divider) | (sixteen_bit ? SPI_CR1_DFF : 0u);
    CADS_LCD_SPI->CR2 = 0u;
    CADS_LCD_SPI->I2SCFGR &= ~SPI_I2SCFGR_I2SMOD;
    CADS_LCD_SPI->CR1 |= SPI_CR1_SPE;
}

void cads_hal_spi_init(void) {
    cads_gpio_init_alternate(CADS_PIN_SPI_SCK_PORT, CADS_PIN_SPI_SCK, 5u, CadsGpioPullDown);
    cads_gpio_init_alternate(CADS_PIN_SPI_MISO_PORT, CADS_PIN_SPI_MISO, 5u, CadsGpioPullNone);
    cads_gpio_init_alternate(
        CADS_PIN_SPI_MOSI_PORT, CADS_PIN_SPI_MOSI, CADS_PIN_SPI_MOSI_AF, CadsGpioPullNone);

    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN;
    (void)RCC->AHB1ENR;

    cads_spi_configure(CADS_LCD_SPI_DIV_SAFE, false);

    NVIC_SetPriority(DMA2_Stream3_IRQn, 6u);
    NVIC_EnableIRQ(DMA2_Stream3_IRQn);
}

/* Which of the two display dividers is currently selected. The touch driver
 * has to drop the bus to a crawl for the XPT2046 and put it back afterwards;
 * remembering the choice here means it cannot accidentally undo a qualified
 * fast-clock setting. */
static cads_spi_speed_t cads_spi_display_speed = CadsSpiSpeedDisplay;

void cads_hal_spi_set_speed(cads_spi_speed_t speed) {
    cads_hal_spi_wait();
    switch(speed) {
    case CadsSpiSpeedTouch:
        cads_spi_configure(CADS_TP_SPI_DIV, false);
        break;
    case CadsSpiSpeedDisplayFast:
        cads_spi_display_speed = speed;
        cads_spi_configure(CADS_LCD_SPI_DIV_FAST, false);
        break;
    case CadsSpiSpeedDisplay:
    default:
        cads_spi_display_speed = CadsSpiSpeedDisplay;
        cads_spi_configure(CADS_LCD_SPI_DIV_SAFE, false);
        break;
    }
}

void cads_hal_spi_restore_display_speed(void) {
    cads_hal_spi_set_speed(cads_spi_display_speed);
}

/* --- Ethernet arbitration ------------------------------------------------- */

#if !CADS_SPI_ETH_COEXIST
static uint32_t cads_eth_saved_maccr = 0u;
static uint32_t cads_eth_claim_depth = 0u;

/* Hard ceiling on the "let an in-flight frame drain" wait below. A maximum
 * 1522-byte frame at 100 Mbit is ~123 us on the wire; 200 us covers it with
 * margin. This exists because DMASR.TS/RS are latched status bits nothing in
 * this driver ever clears once set (see the claim-bus comment) - without a
 * bound the wait is not a drain, it is a permanent hang after the first
 * received frame. */
#define CADS_ETH_DRAIN_TIMEOUT_US 200u

/*
 * Whether the RMII DATA path owns PA7.
 *
 * Not the same question as "is the ETH clock on". The PHY's management
 * interface (MDIO on PA2/MDC on PC1) needs the ETH clock but never touches
 * PA7, so link state and PHY identity can be read with the display running
 * normally. Only bringing up RMII makes PA7 contended.
 *
 * Keying the arbitration off the clock instead would mean every blit stopped
 * and restarted a MAC that was not running, and handed PA7 to a data path that
 * did not exist - pure cost for no correctness.
 */
static bool cads_eth_datapath_active = false;

void cads_hal_spi_set_eth_datapath_active(bool active) {
    cads_eth_datapath_active = active;
}

static bool cads_eth_is_running(void) {
    /* Touching MAC registers with the clock gated would fault, so both
     * conditions have to hold. */
    return cads_eth_datapath_active && (RCC->AHB1ENR & RCC_AHB1ENR_ETHMACEN) != 0u;
}
#else
void cads_hal_spi_set_eth_datapath_active(bool active) {
    (void)active; /* PA7 belongs to the PHY alone after the SB121/SB122 swap */
}
#endif

void cads_hal_spi_claim_bus(void) {
#if !CADS_SPI_ETH_COEXIST
    if(cads_eth_claim_depth++ != 0u) return;

    if(cads_eth_is_running()) {
        cads_eth_saved_maccr = ETH->MACCR & (ETH_MACCR_TE | ETH_MACCR_RE);
        if(cads_eth_saved_maccr) {
            ETH->MACCR &= ~(ETH_MACCR_TE | ETH_MACCR_RE);
            /* Let any frame already in the pipe drain rather than truncating
             * it on the wire - but NEVER spin unbounded here. DMASR.TS (bit 0)
             * and RS (bit 6) are latched, write-1-to-clear completion-status
             * bits, not live "DMA busy" indicators. This driver runs the DMA
             * with no ETH interrupt handler and the TX descriptor's IC bit
             * clear (hal_eth_mac.c), and nothing ever writes DMASR back except
             * the one-shot clear in cads_hal_eth_mac_init(). So once a frame is
             * *received*, RS latches to 1 and stays 1 forever, and an unbounded
             * `while(DMASR & (TS|RS))` would hang this single-threaded loop on
             * the very next display redraw (verified: TX alone does not trip it
             * because IC is clear, but any RX does). Bound the wait, then clear
             * the latch so the next claim starts clean. The receive path works
             * off the descriptor OWN bits, not DMASR, so clearing the status
             * here loses no received frame. */
            uint64_t drain_deadline = cads_hal_ticks_us() + CADS_ETH_DRAIN_TIMEOUT_US;
            while((ETH->DMASR & (ETH_DMASR_TS | ETH_DMASR_RS)) &&
                  cads_hal_ticks_us() < drain_deadline) {
            }
            ETH->DMASR = ETH_DMASR_TS | ETH_DMASR_RS; /* w1c: clear the latch */
        }
    }
    cads_gpio_set_alternate(CADS_PIN_SPI_MOSI_PORT, CADS_PIN_SPI_MOSI, CADS_PIN_SPI_MOSI_AF);
    cads_gpio_set_mode(
        CADS_PIN_SPI_MOSI_PORT,
        CADS_PIN_SPI_MOSI,
        CadsGpioModeAlternate,
        CadsGpioPullNone,
        CadsGpioSpeedHigh);
#endif
}

void cads_hal_spi_release_bus(void) {
#if !CADS_SPI_ETH_COEXIST
    if(--cads_eth_claim_depth != 0u) return;

    cads_hal_spi_wait();

    if(cads_eth_is_running()) {
        /* AF11 = ETH on PA7. */
        cads_gpio_set_alternate(CADS_PIN_SPI_MOSI_PORT, CADS_PIN_SPI_MOSI, 11u);
        ETH->MACCR |= cads_eth_saved_maccr;
        cads_eth_saved_maccr = 0u;
    }
#endif
}

/* --- polled byte transfers (commands, touch) ------------------------------ */

uint8_t cads_hal_spi_transfer(uint8_t value) {
    while(!(CADS_LCD_SPI->SR & SPI_SR_TXE)) {
    }
    *(volatile uint8_t*)&CADS_LCD_SPI->DR = value;
    while(!(CADS_LCD_SPI->SR & SPI_SR_RXNE)) {
    }
    uint8_t received = *(volatile uint8_t*)&CADS_LCD_SPI->DR;
    while(CADS_LCD_SPI->SR & SPI_SR_BSY) {
    }
    return received;
}

void cads_hal_spi_write(const uint8_t* data, size_t length) {
    for(size_t i = 0; i < length; i++) {
        (void)cads_hal_spi_transfer(data[i]);
    }
}

/* --- DMA transmit --------------------------------------------------------- */

bool cads_hal_spi_busy(void) {
    return cads_spi_dma_active;
}

void cads_hal_spi_wait(void) {
    while(cads_spi_dma_active) {
        __WFI();
    }
    while(CADS_LCD_SPI->SR & SPI_SR_BSY) {
    }

    /* The display writes are TX-only (polled cads_hal_spi_transfer() command
     * bytes, or the DMA paths below) and never read DR back. Every one of
     * those leaves a byte sitting in DR with RXNE set, and a second one
     * behind it sets OVR. cads_hal_spi_transfer() assumes RXNE is clear on
     * entry - it waits for RXNE, not for a specific transfer's RXNE - so
     * the touch driver's next polled read (command, then two data bytes)
     * silently desyncs by one transfer: the byte it returns for "high" is
     * really the stale leftover from here, and "low" is really the true
     * high byte. That reads as the 12 bit result pinned to 0-15 with the
     * high byte always exactly 0x00 - confirmed against live captures
     * tonight - not a wiring or timing fault. cads_hal_spi_wait() already
     * runs at the one point every caller agrees the bus is quiescent
     * before doing anything new with it (set_speed, both DMA starts), so
     * draining here fixes every current and future polled-after-DMA
     * sequence in one place rather than patching the touch driver alone.
     * RM0090 28.3.7's documented OVR-clear sequence is read DR then read
     * SR; a single drain is enough since this SPI has no RX FIFO to drain
     * (SPIv1: one byte, not a queue). */
    (void)CADS_LCD_SPI->DR;
    (void)CADS_LCD_SPI->SR;
}

/*
 * Pixel transfers run the bus in 16-bit data frame format.
 *
 * Three reasons, all of which matter:
 *   - The shield's 74HC4040 latches the shift register chain every 16 clocks,
 *     so a 16-bit frame is the natural unit. Two back-to-back 8-bit frames
 *     produce the same clocks but only if the DR is refilled without a gap.
 *   - SPI sends a 16-bit frame most significant byte first, which is exactly
 *     the order the panel wants. That removes the byte swap that would
 *     otherwise be needed on every pixel.
 *   - It halves the number of DMA beats.
 *
 * Commands and parameters stay 8-bit: a command is a single byte with DC low,
 * which a 16-bit frame cannot express.
 */
void cads_hal_spi_write_dma16(const void* data, size_t halfwords) {
    cads_hal_spi_wait();

    /* DFF can only change while the peripheral is disabled. */
    uint32_t cr1 = CADS_LCD_SPI->CR1;
    CADS_LCD_SPI->CR1 = cr1 & ~SPI_CR1_SPE;
    CADS_LCD_SPI->CR1 = (cr1 | SPI_CR1_DFF) & ~SPI_CR1_SPE;
    CADS_LCD_SPI->CR1 |= SPI_CR1_SPE;

    CADS_SPI_DMA_STREAM->CR &= ~DMA_SxCR_EN;
    while(CADS_SPI_DMA_STREAM->CR & DMA_SxCR_EN) {
    }
    CADS_SPI_DMA->LIFCR = DMA_LIFCR_CTCIF3 | DMA_LIFCR_CHTIF3 | DMA_LIFCR_CTEIF3 |
                          DMA_LIFCR_CDMEIF3 | DMA_LIFCR_CFEIF3;

    CADS_SPI_DMA_STREAM->PAR = (uint32_t)&CADS_LCD_SPI->DR;
    CADS_SPI_DMA_STREAM->M0AR = (uint32_t)data;
    CADS_SPI_DMA_STREAM->NDTR = (uint32_t)halfwords;
    CADS_SPI_DMA_STREAM->FCR = 0u;
    CADS_SPI_DMA_STREAM->CR = (CADS_SPI_DMA_CHANNEL << DMA_SxCR_CHSEL_Pos) | DMA_SxCR_MINC |
                              (1u << DMA_SxCR_MSIZE_Pos) | (1u << DMA_SxCR_PSIZE_Pos) |
                              (1u << DMA_SxCR_DIR_Pos) | (2u << DMA_SxCR_PL_Pos) |
                              DMA_SxCR_TCIE | DMA_SxCR_TEIE;

    cads_spi_dma_active = true;
    CADS_LCD_SPI->CR2 |= SPI_CR2_TXDMAEN;
    CADS_SPI_DMA_STREAM->CR |= DMA_SxCR_EN;
}

/** Return the bus to 8-bit frames for commands and touch. */
void cads_hal_spi_end_16bit(void) {
    cads_hal_spi_wait();
    uint32_t cr1 = CADS_LCD_SPI->CR1;
    CADS_LCD_SPI->CR1 = cr1 & ~SPI_CR1_SPE;
    CADS_LCD_SPI->CR1 = (cr1 & ~SPI_CR1_DFF) & ~SPI_CR1_SPE;
    CADS_LCD_SPI->CR1 |= SPI_CR1_SPE;
}

void cads_hal_spi_write_dma(const void* data, size_t length) {
    cads_hal_spi_wait();

    /* The stream must be off before its configuration can be touched. */
    CADS_SPI_DMA_STREAM->CR &= ~DMA_SxCR_EN;
    while(CADS_SPI_DMA_STREAM->CR & DMA_SxCR_EN) {
    }
    CADS_SPI_DMA->LIFCR = DMA_LIFCR_CTCIF3 | DMA_LIFCR_CHTIF3 | DMA_LIFCR_CTEIF3 |
                          DMA_LIFCR_CDMEIF3 | DMA_LIFCR_CFEIF3;

    CADS_SPI_DMA_STREAM->PAR = (uint32_t)&CADS_LCD_SPI->DR;
    CADS_SPI_DMA_STREAM->M0AR = (uint32_t)data;
    CADS_SPI_DMA_STREAM->NDTR = (uint32_t)length;
    CADS_SPI_DMA_STREAM->FCR = 0u; /* direct mode */
    CADS_SPI_DMA_STREAM->CR = (CADS_SPI_DMA_CHANNEL << DMA_SxCR_CHSEL_Pos) |
                              DMA_SxCR_MINC | (1u << DMA_SxCR_DIR_Pos) /* mem -> periph */ |
                              (2u << DMA_SxCR_PL_Pos) /* high priority */ | DMA_SxCR_TCIE |
                              DMA_SxCR_TEIE;

    cads_spi_dma_active = true;
    CADS_LCD_SPI->CR2 |= SPI_CR2_TXDMAEN;
    CADS_SPI_DMA_STREAM->CR |= DMA_SxCR_EN;
}

void DMA2_Stream3_IRQHandler(void) {
    uint32_t status = CADS_SPI_DMA->LISR;

    if(status & DMA_LISR_TEIF3) {
        CADS_SPI_DMA->LIFCR = DMA_LIFCR_CTEIF3;
        cads_spi_dma_active = false;
        CADS_LCD_SPI->CR2 &= ~SPI_CR2_TXDMAEN;
        cads_hal_panic("SPI DMA transfer error");
    }

    if(status & DMA_LISR_TCIF3) {
        CADS_SPI_DMA->LIFCR = DMA_LIFCR_CTCIF3;
        CADS_SPI_DMA_STREAM->CR &= ~DMA_SxCR_EN;
        /* The last byte is still shifting out of the peripheral when the DMA
         * says it is done; the display driver must not raise CS before BSY
         * clears, so hold the flag until the shift register is empty. */
        while(CADS_LCD_SPI->SR & SPI_SR_BSY) {
        }
        CADS_LCD_SPI->CR2 &= ~SPI_CR2_TXDMAEN;
        cads_spi_dma_active = false;
    }
}
