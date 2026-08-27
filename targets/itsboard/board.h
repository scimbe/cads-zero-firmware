/*
 * CaDS Zero - ITSboard pin map.
 *
 * Single source of truth for every pin this firmware touches. Derived from:
 *   - NUCLEO-F429ZI (UM1974) Arduino connector mapping
 *   - Waveshare 4inch TFT Touch Shield schematic + user manual
 *   - The ITS adapter board wiring as used by the existing ITS_BRD_LIB
 *
 * Read docs/SAFETY.md before adding anything here. In short:
 *   - PA13/PA14 are SWDIO/SWCLK. Reconfiguring them costs you debug access.
 *   - PH0/PH1 carry the 8 MHz clock from the ST-Link (HSE bypass).
 *   - PF0..PF7 and PG0..PG5 are INPUTS on the adapter board. Driving them as
 *     outputs risks contention with whatever is wired to the adapter.
 *   - The RMII pins belong to the Ethernet MAC and nothing else.
 */

#ifndef CADS_BOARD_H
#define CADS_BOARD_H

#include "stm32f4xx.h"

/* --- clock tree -------------------------------------------------------------
 * HSE is the 8 MHz MCO output of the on-board ST-Link, fed in as a bypass
 * clock. PLL: 8 / M(8) * N(360) / P(2) = 180 MHz, with over-drive enabled and
 * 5 flash wait states. These are the documented maximum-performance settings
 * for the F429 at 3.3 V; do not raise them.
 */
#define CADS_HSE_HZ      8000000u
#define CADS_SYSCLK_HZ   180000000u
#define CADS_HCLK_HZ     180000000u
#define CADS_PCLK1_HZ    45000000u  /* AHB / 4 */
#define CADS_PCLK2_HZ    90000000u  /* AHB / 2 */

/* --- display: Waveshare 4" TFT Touch Shield, ILI9486 ------------------------
 * The shield does NOT wire the ILI9486 to SPI directly. SPI bytes are shifted
 * into a 74HC4040 counter plus two 74HC4094 shift registers, which present a
 * 16-bit parallel word to the panel. Consequences that shape the driver:
 *   - the bus is WRITE ONLY: the panel cannot be read back, so there is no
 *     ID check and no read-modify-write on video memory;
 *   - one pixel costs 16 SPI clocks, so the pixel rate is SPI_CLK / 16.
 */
#define CADS_LCD_WIDTH   480
#define CADS_LCD_HEIGHT  320

#define CADS_LCD_SPI            SPI1

#define CADS_PIN_SPI_SCK_PORT   GPIOA
#define CADS_PIN_SPI_SCK        5u
#define CADS_PIN_SPI_MISO_PORT  GPIOA
#define CADS_PIN_SPI_MISO       6u

/* --- THE PA7 CONFLICT -------------------------------------------------------
 * Arduino D11 (SPI1_MOSI, the display's data line) lands on PA7 in the
 * board's default strapping. PA7 is also the ONLY pin on the STM32F429 that
 * can carry ETH_RMII_CRS_DV. Display and Ethernet therefore cannot both own
 * the pin, and whichever driver initialises last wins the alternate-function
 * mux. This is a hardware fact, not something software can wish away.
 *
 * UM1974 provides the escape hatch: solder bridges SB121/SB122 select which
 * MCU pin D11 is bonded to.
 *
 *   SB121 ON,  SB122 OFF  (factory default)  -> D11 = PA7
 *   SB121 OFF, SB122 ON                      -> D11 = PB5
 *
 * CADS_SPI_MOSI_ON_PB5 = 0 : stock board. Display and Ethernet are mutually
 *     exclusive; cads_hal_spi_claim_bus() time-slices PA7 and Ethernet RX
 *     loses frames during display bursts.
 * CADS_SPI_MOSI_ON_PB5 = 1 : after the bridge swap. PA7 belongs to the PHY
 *     alone and both subsystems run concurrently at full speed.
 */
#ifndef CADS_SPI_MOSI_ON_PB5
#define CADS_SPI_MOSI_ON_PB5 0
#endif

#if CADS_SPI_MOSI_ON_PB5
#define CADS_PIN_SPI_MOSI_PORT  GPIOB
#define CADS_PIN_SPI_MOSI       5u
#define CADS_SPI_ETH_COEXIST    1
#else
#define CADS_PIN_SPI_MOSI_PORT  GPIOA
#define CADS_PIN_SPI_MOSI       7u
#define CADS_SPI_ETH_COEXIST    0
#endif

#define CADS_PIN_SPI_MOSI_AF    5u  /* AF5 = SPI1 on both PA7 and PB5 */

#define CADS_PIN_LCD_CS_PORT    GPIOD   /* Arduino D10 */
#define CADS_PIN_LCD_CS         14u
#define CADS_PIN_LCD_DC_PORT    GPIOF   /* Arduino D7  */
#define CADS_PIN_LCD_DC         13u
#define CADS_PIN_LCD_RST_PORT   GPIOF   /* Arduino D8  */
#define CADS_PIN_LCD_RST        12u
#define CADS_PIN_LCD_BL_PORT    GPIOD   /* Arduino D9, drives an S8050 */
#define CADS_PIN_LCD_BL         15u

/* SPI1 sits on APB2 (90 MHz). The 74HC4094 chain is the limiting factor, not
 * the panel. The proven-good divider from the existing ITS firmware is /16;
 * anything faster must be qualified on real hardware one step at a time.
 * See docs/SAFETY.md "Raising the SPI clock". */
#define CADS_LCD_SPI_DIV_SAFE   16u  /*  5.6 MHz -> 351 kpx/s */
#define CADS_LCD_SPI_DIV_FAST   8u   /* 11.3 MHz -> 703 kpx/s, qualified in M1 */
#define CADS_TP_SPI_DIV         128u /* XPT2046 needs a slow clock */

/* --- touch: XPT2046, shares SPI1 ------------------------------------------ */
#define CADS_PIN_TP_CS_PORT     GPIOF   /* Arduino D4 */
#define CADS_PIN_TP_CS          14u
#define CADS_PIN_TP_IRQ_PORT    GPIOE   /* Arduino D3 */
#define CADS_PIN_TP_IRQ         13u
/* TP_BUSY is PE9 (Arduino D6) per the board's official pin table
 * (ITS_BRD_HW/ITS-BRD-NucleoPins.xlsx). The older lab sources say PB10, which
 * does not match the table or the shield manual.
 *
 * Rather than pick a side, the driver simply does not use it: the XPT2046's
 * BUSY line is optional, and a fixed conversion delay is both sufficient and
 * one less disputed dependency. Kept here for documentation only. */
#define CADS_PIN_TP_BUSY_PORT   GPIOE
#define CADS_PIN_TP_BUSY        9u

/* The shield's microSD slot shares the SPI bus; SD_CS is PE11 (Arduino D5),
 * confirmed by the board's official pin table. Unused - the user runs without
 * a card, and storage lives in internal flash. */
#define CADS_PIN_SD_CS_PORT     GPIOE
#define CADS_PIN_SD_CS          11u

/* --- ITS adapter board I/O --------------------------------------------------
 *
 * Confirmed against the board's official pin table and the manufacturer's own
 * hardware test (ITS-BRD/its_brd_tst, Programs/GPIOTest):
 *
 *   OUT0..7   PD0..PD7   LED bank, ACTIVE HIGH
 *                        GPIOTest walks them with BSRR = 1<<i to light each.
 *   OUT8..15  PE0..PE7   LED bank, active high
 *   IN0..7    PF0..PF7   push buttons S0..S7, ACTIVE LOW
 *                        GPIOTest waits on (GPIOF->IDR & (1<<i)) != 0, i.e.
 *                        a press drives the line to ground. Internal pull-ups
 *                        are therefore correct.
 *   INT0..5   PG0..PG5   NOT buttons. General purpose inputs that GPIOTest
 *                        exercises by asking the operator to jumper OUT0 to
 *                        INTx. Available for external signals; EXTI capable.
 */
#define CADS_PIN_OUT_LOW_PORT   GPIOD
#define CADS_PIN_OUT_HIGH_PORT  GPIOE
#define CADS_PIN_IN_PORT        GPIOF
#define CADS_PIN_INT_PORT       GPIOG
#define CADS_ADAPTER_IO_MASK    0x00FFu  /* bits 0..7 on each of the above */
#define CADS_ADAPTER_INT_MASK   0x003Fu  /* bits 0..5 on GPIOG */

/* --- CN8 timer breakout: frequency/period counter --------------------------
 *
 * docs/ROADMAP.md's "Frequency/period counter on an INT line via timer
 * input capture" bullet asks for the exact pin-to-timer-channel mapping
 * to be confirmed before wiring. Its own named source
 * (ITS_BRD_HW/ITS-BRD-NucleoPins.xlsx) is not archived in this
 * repository, so this was confirmed two other ways instead:
 *
 *   1. The project's own schematic (docs/reference/datasheets/
 *      ITSBRD-schematic-Jaehnichen-HAW-rev02.pdf, the "timers" page)
 *      labels CN8 pin 5 with the net name "TIM2_3" on physical pin PB10.
 *   2. The archived F415/417 sibling datasheet's alternate function
 *      table independently confirms PB10: TIM2_CH3 via AF1 (matching
 *      the schematic's own net label) - a cross-check the README's own
 *      caution about not trusting sibling-part *timing/electrical*
 *      numbers does not cover, since AF pin routing is a shared-silicon
 *      fact across the whole RM0090-covered family, not a per-part
 *      electrical spec.
 *
 * NOT one of the adapter's INT0..5 lines (PG0..PG5) on purpose: none of
 * them carry any timer alternate function at all per that same AF table
 * (their only non-GPIO AF is FSMC_A1x), and this exact tradeoff was
 * already flagged in docs/HARDWARE.md's "Capability this board has that
 * the firmware does not yet use" section - the timer breakout exists
 * "for this kind of use... without having to repurpose an OUT/IN pin
 * that already has a job". PB10 is confirmed unclaimed elsewhere in this
 * codebase: not on the RMII reserved-pin list
 * (cads_hal_pin_is_reserved()/docs/SAFETY.md), and the disputed old
 * "PB10 = touch panel BUSY" claim a few lines above this is explicitly
 * not what any driver here actually uses.
 */
#define CADS_PIN_FREQCOUNTER_PORT GPIOB
#define CADS_PIN_FREQCOUNTER_PIN  10u
#define CADS_PIN_FREQCOUNTER_AF   1u

/* --- PWM generator on an adapter OUT pin ------------------------------------
 *
 * docs/ROADMAP.md's own bullet: "any adapter output pin on a timer
 * channel can be reconfigured AF instead of GPIO push-pull... PD/PE
 * pins have mixed timer support, some OUT pins may be GPIO-only" -
 * checked all sixteen (PD0..7/PE0..7) against the sibling datasheet's
 * alternate function table rather than guessing which ones qualify.
 * Fourteen of them carry no timer AF at all (FSMC/USART/CAN only, this
 * board's OUT pins sit on the same silicon pins ST wired for its FSMC
 * parallel memory bus on other boards). Two more - PD2 (TIM3_ETR) and
 * PE0/PE7 (TIM4_ETR/TIM1_ETR) - carry a timer function that is an
 * External Trigger *input*, not a PWM-capable output channel, so they
 * do not count either. Only PE5 and PE6 (OUT13, OUT14) have a genuine
 * output-compare channel: TIM9_CH1 and TIM9_CH2 respectively, both AF3
 * (RM0090's own CCxS/OCxM text, not just the datasheet table position,
 * confirms PWM mode 1 = OC1M 110 works on a plain output-compare
 * channel like these). Using PE5/TIM9_CH1 - the roadmap bullet asks for
 * one PWM generator, not both channels.
 *
 * TIM9 is a general-purpose timer, not TIM1/TIM2/TIM3/TIM8 from the
 * CN8 breakout table - it needs its own clock check: TIM9/10/11 are on
 * APB2 (hal_clock.c: PPRE2 = DIV2, so APB2 = 90 MHz), and RM0090's same
 * doubling rule already applied to TIM2/TIM6 this milestone (APB
 * prescaler != 1 => TIMxCLK = 2 x APBx) makes TIM9CLK = 180 MHz - twice
 * TIM2's 90 MHz, verified by the same rule, not re-read from the PDF a
 * third time.
 *
 * PE5 is confirmed on this board's own schematic (net "OUT13"). Using
 * it for PWM claims it away from apps/gpio's plain OUT13 LED for as
 * long as the PWM generator command runs, the same "one pin, two jobs,
 * never at once" pattern hal_spi.c's PA7 claim/release already
 * established for a much higher-stakes case - the PWM driver's own
 * stop() returns the pin to plain GPIO output, matching what
 * cads_hal_freqcounter_stop()'s fix already established as this
 * project's contract for a driver that borrows a pin temporarily.
 */
#define CADS_PIN_PWM_PORT GPIOE
#define CADS_PIN_PWM_PIN  5u
#define CADS_PIN_PWM_AF   3u

/* --- Nucleo-144 on-board indicators --------------------------------------- */
#define CADS_PIN_LED_GREEN_PORT GPIOB
#define CADS_PIN_LED_GREEN      0u
#define CADS_PIN_LED_BLUE_PORT  GPIOB
#define CADS_PIN_LED_BLUE       7u
#define CADS_PIN_LED_RED_PORT   GPIOB
#define CADS_PIN_LED_RED        14u
#define CADS_PIN_USER_BTN_PORT  GPIOC
#define CADS_PIN_USER_BTN       13u

/* --- console: USART3 routed to the ST-Link virtual COM port ---------------- */
#define CADS_CONSOLE_UART       USART3
#define CADS_CONSOLE_BAUD       115200u
#define CADS_PIN_UART_TX_PORT   GPIOD
#define CADS_PIN_UART_TX        8u
#define CADS_PIN_UART_RX_PORT   GPIOD
#define CADS_PIN_UART_RX        9u
#define CADS_UART_AF            7u

/* --- WiFi co-processor: ESP32 over USART6, PPPoS -----------------------------
 * PC6/PC7 are free (not RMII, not adapter I/O, not the SPI3/timer headers
 * used elsewhere) - see docs/reference/wifi-coprocessor.md for the wiring
 * rationale and the full link protocol. AF8 carries USART6 on this port,
 * unlike USART3's AF7 above - confirm against the datasheet before reuse.
 * High baud because PPP framing (HDLC byte-stuffing) inflates the wire bytes
 * per IP byte; 460800 keeps the link itself well clear of being the
 * throughput bottleneck relative to the WiFi hop behind it. */
#define CADS_WIFI_UART           USART6
#define CADS_WIFI_BAUD           460800u
#define CADS_PIN_WIFI_TX_PORT    GPIOC
#define CADS_PIN_WIFI_TX         6u
#define CADS_PIN_WIFI_RX_PORT    GPIOC
#define CADS_PIN_WIFI_RX         7u
#define CADS_WIFI_UART_AF        8u

/* --- Ethernet: LAN8742A over RMII ------------------------------------------
 * PA1 REF_CLK, PA2 MDIO, PC1 MDC, PA7 CRS_DV, PC4 RXD0, PC5 RXD1,
 * PG2 RXER, PG11 TX_EN, PG13 TXD0, PB13 TXD1.
 * PA7 is the contended pin - see CADS_SPI_MOSI_ON_PB5 above; hal_spi.c owns
 * its alternate-function switching, so it has no macro here.
 * MDIO/MDC are hal_eth_mdio.c's own pins, also not listed here, since that
 * driver predates this RMII data-path macro set and names them inline.
 */
#define CADS_ETH_PHY_ADDR       0u
#define CADS_ETH_AF             11u /* AF11 = ETH on every RMII pin on this part */

#define CADS_PIN_ETH_REF_CLK_PORT GPIOA
#define CADS_PIN_ETH_REF_CLK      1u
#define CADS_PIN_ETH_RXD0_PORT    GPIOC
#define CADS_PIN_ETH_RXD0         4u
#define CADS_PIN_ETH_RXD1_PORT    GPIOC
#define CADS_PIN_ETH_RXD1         5u
#define CADS_PIN_ETH_RXER_PORT    GPIOG
#define CADS_PIN_ETH_RXER         2u
#define CADS_PIN_ETH_TX_EN_PORT   GPIOG
#define CADS_PIN_ETH_TX_EN        11u
#define CADS_PIN_ETH_TXD0_PORT    GPIOG
#define CADS_PIN_ETH_TXD0         13u
#define CADS_PIN_ETH_TXD1_PORT    GPIOB
#define CADS_PIN_ETH_TXD1         13u

/* --- internal flash storage volume ---------------------------------------- */
/* Everything below CADS_FS_BASE - the firmware in bank 1 and the reserved gap
 * of bank 2 sectors 12..16 (docs/SAFETY.md section 4) - the storage driver
 * never erases or programs. CADS_FLASH_APP_BASE names the start of that
 * range for anything that wants to checksum it (the M4 hardware gate does),
 * without repeating the address as an unexplained literal. */
#define CADS_FLASH_APP_BASE     0x08000000u
#define CADS_FS_BASE            0x08120000u  /* bank 2, sector 17 */
#define CADS_FS_SIZE            (896u * 1024u)
#define CADS_FS_BLOCK_SIZE      (128u * 1024u)
#define CADS_FS_FIRST_SECTOR    17u
#define CADS_FS_SECTOR_COUNT    7u

#endif /* CADS_BOARD_H */
