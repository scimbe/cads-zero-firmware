# The ITSboard, and how it compares to a Flipper Zero

Everything here was verified against the physical board over ST-Link, or read
out of the schematics in the vendor documentation. Where something is assumed
rather than verified it says so.

---

## 1. What the board is

Three stacked pieces:

1. **NUCLEO-F429ZI Rev.B** — STM32F429ZIT6, on-board ST-Link/V2-1
2. **ITS adapter board** — brings 16 outputs, 8 inputs and 6 interrupt lines out
3. **Waveshare 4inch TFT Touch Shield** — ILI9486 480×320 with XPT2046 touch

Probe identity, read with `st-info --probe`:

```
version:    V2J45S30
serial:     066FFF565282494867161033
flash:      2097152 (pagesize: 16384)
sram:       262144
chipid:     0x419          -> STM32F42x/F43x
```

## 2. MCU

| | |
|---|---|
| Core | Cortex-M4F, FPv4-SP-D16, hard float |
| Clock | 180 MHz from an 8 MHz HSE bypass (the ST-Link's MCO) |
| PLL | M=8, N=360, P=2, Q=7, over-drive on, 5 flash wait states |
| Buses | AHB 180 MHz, APB1 45 MHz, APB2 90 MHz |
| Flash | 2 MB in **two banks** of 1 MB — read-while-write across banks |
| SRAM | 192 KB contiguous at `0x20000000` (SRAM1 112K + SRAM2 16K + SRAM3 64K) |
| CCM | 64 KB at `0x10000000` — **no DMA access** |
| Notable | LTDC, **DMA2D (Chrom-ART)**, ETH MAC, USB OTG FS |

DMA2D is worth calling out: it does CLUT expansion (L4/L8 → RGB565) and
rectangle fills in hardware. That is exactly the operation the indexed canvas
needs on every flush, and nothing comparable exists on a Flipper's STM32WB55.

## 3. Display

Waveshare 4inch TFT Touch Shield, ILI9486, 480×320, RGB565.

**The bus is not plain SPI.** MOSI feeds a 74HC4040 counter and two cascaded
74HC4094 shift registers, which present a 16-bit parallel word to the panel:

```
MCU SPI1 ──MOSI──> 74HC4094 ×2 ──D[15:0]──> ILI9486
             │        ▲
             └─SCLK──>74HC4040 ──CLK/16──> latch + panel WR
```

Two consequences drive the whole graphics design:

- **Write-only.** No register readback, no ID check, no read-modify-write on
  video memory. The RAM framebuffer is the only truth.
- **16 SPI clocks per pixel.** Pixel rate is `SPI_CLK / 16`.

Measured on the real board at `/16` (5.625 MHz SPI):

| | |
|---|---|
| Full screen (153 600 px) | **448 ms** |
| Throughput | **342 kpixel/s** (97 % of the 351 kpx/s theoretical) |
| 40×40 dirty rect | 4.7 ms |

This is why dirty-rectangle tracking is a requirement rather than an
optimisation, and why raising the divider is the first item in M1.

Protocol quirk, reproduced from the vendor code that has driven this shield for
years: a **command is one byte with DC low**, a **parameter or pixel is two
bytes with DC high**. The asymmetry belongs to the glue logic, not the ILI9486.

### Pin map

| Signal | Arduino | STM32 | Notes |
|---|---|---|---|
| SCLK | D13 | PA5 | |
| MISO | D12 | PA6 | touch only |
| MOSI | D11 | **PA7** | **contended, see §6** |
| LCD_CS | D10 | PD14 | |
| LCD_BL | D9 | PD15 | TIM4_CH4 PWM, AF2 |
| LCD_RST | D8 | PF12 | |
| LCD_DC | D7 | PF13 | |
| TP_BUSY | D6 | PB10 | adapter remaps this; the shield manual says PB4 |
| SD_CS | D5 | PE11? | **unverified, never driven** |
| TP_CS | D4 | PF14 | |
| TP_IRQ | D3 | PE13 | |

The shield carries a microSD slot on the same SPI bus. Whether the ITS adapter
routes `SD_CS` through is unverified, and the user has confirmed no card is in
use, so the firmware never drives that pin. Storage lives in internal flash.

## 4. ITS adapter I/O

| Name | Port | Direction |
|---|---|---|
| OUT0..7 | PD0..PD7 | output (LED bank) |
| OUT8..15 | PE0..PE7 | output (LED bank) |
| IN0..7 | PF0..PF7 | input, pulled up |
| INT0..5 | PG0..PG5 | input, pulled up, EXTI capable |

### The buttons

**S0..S7 are PF0..PF7 (IN0..IN7), active low.** Settled by the manufacturer's
own hardware test, `ITS-BRD/its_brd_tst`, `Programs/GPIOTest/Src/main.c`:

```c
sprintf(txt, "Bitte die Taste %d druecken", i);
while ((GPIOF->IDR & (1 << i)) != 0) { }   /* waits for the line to go LOW */
```

A press pulls the line to ground, so internal pull-ups are the correct
configuration.

**INT0..INT5 (PG0..PG5) are not buttons.** The same test exercises them by
asking the operator to *jumper OUT0 to INTx with a wire* - they are
general-purpose inputs brought out for external signals, EXTI capable. Any
design that treats them as a button cluster is wrong.

**The OUT LEDs are active high.** GPIOTest walks them with
`GPIOD->BSRR = 1 << i` to light each in turn.

!!! note "A correction worth recording"
    An earlier reading of a camera frame showed eight LEDs lit while the output
    banks were held at `0x0000`, and this was written up as "the OUT LEDs are
    active low". That was wrong. The lit LEDs were almost certainly the eight
    **input** state indicators, showing IN0..IN7 idle high with no button
    pressed. Inferring electrical polarity from a photograph of a board whose
    silkscreen was not legible was a guess dressed up as a measurement; the
    manufacturer's test code settles it in one line.

    Confirmed after the fact against the adapter's own schematic (see below):
    that guess was right. PF0..PF7 feed an SN74LVC245 buffer whose B side
    drives both connector IN0..7 and a bank of eight **red** LEDs - an input
    state indicator, not an output.

### Schematic-level confirmation and what it adds

The adapter's actual schematic (`ITSBRD.pdf` by Tobias Jähnichen, HAW Hamburg
Labor für Technische Informatik, rev. 02 2020-12-16, archived at
[`docs/reference/datasheets/`](reference/datasheets/README.md)) confirms every pin
assignment above at the circuit level and adds detail the manufacturer's test
code did not need to mention:

- **Inputs and outputs go through SN74LVC245 buffer ICs**, not straight to the
  MCU pins. IN0..7 (U1) and OUT0..7/OUT8..15 (U2/U3) are each buffered, with
  10 kΩ pull-ups to 3V3 on the input side. The MCU sees a clean logic load
  either way; plain push-pull output / pulled-up input, exactly what this
  firmware already does, is the correct interface.
- **Three LED banks, one per direction:** red (D0..D7) on the input buffer's
  A side, blue (D8..D15) on OUT0..7, green (D16..D23) on OUT8..15. 24 discrete
  LEDs, not the 16 the OUT-only view suggested.
- **INT0..5 (PG0..5) are unbuffered**, straight from the MCU to connector
  `CN3`, labelled `AUX0..5` on the silkscreen - "AUX", not "INT". Consistent
  with the manufacturer's test procedure treating them as external-signal
  inputs rather than a button cluster.

### Capability this board has that the firmware does not yet use

All confirmed present and wired on the schematic, none of it touched by any
code in this repository yet:

| Bus / feature | Pins | Chip | Header |
|---|---|---|---|
| I2C1 | PB8 (SCL) / PB9 (SDA) | PCA9306 level shifter | `CN5` (3V3) and `CN6` (5V) |
| RS232 | PC10 / PC11 → USART6 | MAX3232ECWE | D-sub-style serial header |
| SPI3 / I2S3 | PC10 SCK, PC12 MOSI, PC11 MISO, PA15 NSS | — (direct) | `CN7` |
| CAN1 | PD0 (RX) / PD1 (TX) | SN65HVD231D transceiver | `CN2`-adjacent CAN header |
| ADC | PF8/PF9/PF10, PB1, PC0/PC2/PC3 | STM32 internal ADC3/12/123 | `CN4`, 7 channels |
| DAC | PA4 | STM32 internal DAC1 | `CN4` |
| Timer breakout | PE8/PE10/PE14, PB10/PB11, PB0, PC6/PC7/PC8/PC9 | TIM1/2/3/8 | `CN8`, 10 channels |

I2C1 and the timer breakout are the two most directly useful for the GPIO
Swiss-army-knife plan in `docs/ROADMAP.md`: an I2C bus scanner is close to
free given the level-shifted header already exists, and the timer breakout
gives input-capture-capable pins for the frequency/duty-cycle tool without
having to repurpose an adapter pin that already has another job.

### What this board does *not* have

**No SD/MMC card slot. No SPI flash.** Both exist on a *different, older* lab
board (STM32F417ZG-based, referred to in its own docs as the "TI-C-Board"),
whose schematic is archived alongside this one for lineage but describes
different hardware with **opposite** GPIO polarity (its Port E is input, Port
G is output - the reverse of PD/PE-output, PF/PG-input here). Storage on this
firmware is internal flash via littlefs (see M4 in `docs/ROADMAP.md`) because
that is what this board actually has, not because SD support was deferred.

## 5. Ethernet

LAN8742A over RMII, PHY address 0.

`PA1` REF_CLK · `PA2` MDIO · `PC1` MDC · `PA7` CRS_DV · `PC4` RXD0 ·
`PC5` RXD1 · `PG2` RXER · `PG11` TX_EN · `PG13` TXD0 · `PB13` TXD1

## 6. The PA7 conflict

**`SPI1_MOSI` and `ETH_RMII_CRS_DV` are the same pin.**

`ETH_RMII_CRS_DV` has exactly one possible location on the STM32F429: PA7. The
Waveshare shield's data line arrives on Arduino D11, which the Nucleo bonds to
PA7 by default. Only one alternate function can own a pin, so whichever driver
initialises last wins.

This is not theoretical. In the ITS lab's existing `Stack` project,
`GUI_init()` claims PA7 for SPI1 and `HAL_ETH_MspInit()` subsequently claims it
for the MAC — after which the display is mute. Their `SPI4W_Write_Byte()`
mitigates it by stopping the MAC, flipping the AF, sending **one byte**, and
restarting the MAC. Correct, but it makes DMA impossible and tears the receiver
down 150 000 times per full-screen redraw.

### What this firmware does

`cads_hal_spi_claim_bus()` / `cads_hal_spi_release_bus()` do the same dance
**per blit** instead of per byte: one stop, one DMA transfer of an entire
rectangle, one restart. Same correctness, three orders of magnitude fewer MAC
restarts, and DMA becomes usable.

### The real fix

UM1974 §6.9:

> **SB121, SB122 (D11)** — ON, OFF: D11 (Pin 14 of CN7) is connected to STM32
> **PA7** (SPI_A_MOSI/TIM_E_PWM1). OFF, ON: D11 (Pin 14 of CN7) is connected to
> STM32 **PB5** (SPI_A_MOSI/TIM_D_PWM2).

Swapping the two bridges moves MOSI to PB5, leaves PA7 to the PHY, and lets both
subsystems run concurrently at full speed. Build with
`-DCADS_SPI_MOSI_ON_PB5=1` afterwards and all the arbitration compiles away.

It is a soldering operation on the Nucleo, reversible, and **the user's
decision**. Until then the firmware defaults to the time-slicing path.

## 7. Compared to a Flipper Zero

| | Flipper Zero (STM32WB55RG) | ITSboard (STM32F429ZI) |
|---|---|---|
| Core | Cortex-M4 @ 64 MHz | Cortex-M4F @ **180 MHz** |
| Flash | 1 MB | **2 MB**, dual bank |
| SRAM | 256 KB | 192 KB + 64 KB CCM |
| Display | ST7565R 128×64 mono | **ILI9486 480×320 colour** |
| Input | 5-way + back buttons | **resistive touch** + 14 adapter lines |
| Graphics accel | none | **DMA2D** |
| Network | BLE | **100 Mbit Ethernet** |
| Storage | microSD | internal flash (littlefs) |
| Sub-GHz | CC1101 | — |
| NFC | ST25R3916 | — |
| 125 kHz RFID | yes | — |
| Infrared | yes | — |
| iButton / 1-Wire | yes | — |
| USB | device, HID/CDC | OTG FS present, unused |

**What ports:** the OS, the GUI framework, the app model, storage, the CLI, the
desktop and mascot, games and utilities.

**What cannot port:** every radio feature. Sub-GHz, NFC, RFID, IR and iButton
are silicon this board does not have, and no amount of software substitutes for
a missing transceiver.

**What replaces them:** the Ethernet stack. A CLI over TCP, a web status page,
screen streaming to a host, and network tooling are things a Flipper cannot do
at all, and they use the hardware advantage this board actually has.
