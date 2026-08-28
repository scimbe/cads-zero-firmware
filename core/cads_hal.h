/*
 * CaDS Zero - hardware abstraction layer.
 *
 * Everything above this header is portable C: it compiles unchanged for the
 * ITSboard and for the host simulator. Two implementations exist:
 *
 *   targets/itsboard/hal/   real STM32F429 peripherals
 *   targets/sim/            SDL2-backed simulation of the whole rig
 *
 * Keeping the surface this narrow is what makes the simulator honest - if a
 * feature is not expressible here, it cannot silently work in only one of the
 * two worlds.
 */

#ifndef CADS_HAL_H
#define CADS_HAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- board geometry --------------------------------------------------------
 *
 * These are the COMPILE TIME maxima, and they exist only because the canvas
 * allocates its framebuffer statically - this device has no allocator, so the
 * buffer's size has to be known to the linker.
 *
 * Everything else should ask cads_hal_board_info() at run time rather than
 * assuming these values. A layout written against CADS_DISPLAY_WIDTH is a
 * layout that breaks the day the panel changes; one written against
 * info->display_width adapts. Porting to a different panel then means editing
 * one board header and one descriptor, not hunting constants through the GUI.
 */
#ifndef CADS_DISPLAY_WIDTH
#define CADS_DISPLAY_WIDTH  480
#endif
#ifndef CADS_DISPLAY_HEIGHT
#define CADS_DISPLAY_HEIGHT 320
#endif

/* --- memory placement -------------------------------------------------------
 *
 * On the board, anything a DMA controller will read must live in SRAM: the
 * STM32F4's CCM is invisible to every DMA engine, and a transfer sourced from
 * there silently produces nothing. The linker script provides a .dmaram
 * section whose placement is therefore guaranteed and visible in the map file.
 *
 * The simulator has no such distinction, and Mach-O rejects a bare section
 * name outright, so the attribute has to be target conditional rather than
 * written inline at each buffer.
 */
#if defined(CADS_TARGET_ITSBOARD)
#define CADS_DMA_SECTION __attribute__((section(".dmaram")))
#define CADS_CCM_SECTION __attribute__((section(".ccm")))
#else
#define CADS_DMA_SECTION
#define CADS_CCM_SECTION
#endif

/* --- board identity and capabilities ---------------------------------------
 *
 * The point of this struct is that the layers above it never have to know
 * which board they are on. An app asks "is there a network?" rather than
 * testing for the ITSboard; a widget asks how many soft keys exist rather than
 * assuming eight. Swapping the adapter, the Nucleo or the panel then touches
 * the target directory and nothing else.
 */
typedef struct {
    const char* board_name;   /**< e.g. "ITSboard (NUCLEO-F429ZI)"           */
    const char* mcu_name;     /**< e.g. "STM32F429ZI"                        */
    uint32_t cpu_hz;

    uint16_t display_width;   /**< <= CADS_DISPLAY_WIDTH                     */
    uint16_t display_height;  /**< <= CADS_DISPLAY_HEIGHT                    */
    bool display_readable;    /**< false when the bus is write-only, as here */

    uint8_t button_count;     /**< physical keys available as soft keys      */
    bool has_touch;
    bool has_network;
    bool has_storage;

    uint32_t flash_bytes;     /**< usable application flash                  */
    uint32_t ram_bytes;       /**< DMA-capable RAM                           */

    /**
     * Measured pixel throughput of the display path, in pixels per second.
     *
     * Published because it is a first-class design input, not trivia: on this
     * board it is ~342 000, which makes a full-screen redraw cost 448 ms and
     * forces dirty-rectangle rendering. A GUI that wants to decide whether an
     * animation is affordable should ask, not guess.
     */
    uint32_t display_pixels_per_second;
} cads_board_info_t;

/** Never NULL, valid for the lifetime of the program. */
const cads_board_info_t* cads_hal_board_info(void);

/* --- lifecycle ----------------------------------------------------------- */

/** Clocks, flash latency, core caches. Runs before the C runtime is usable,
 *  so it must not touch initialised data. */
void cads_hal_early_init(void);

/** Everything else: GPIO, timers, console, display, input. */
void cads_hal_init(void);

/* --- time ---------------------------------------------------------------- */

uint32_t cads_hal_ticks_ms(void);
uint64_t cads_hal_ticks_us(void);
void cads_hal_delay_us(uint32_t us);
void cads_hal_delay_ms(uint32_t ms);

/* --- console (USART3 -> ST-Link VCP on hardware, stdout in the simulator) - */

void cads_hal_console_init(uint32_t baud);
void cads_hal_console_write(const void* data, size_t length);

/**
 * Non-blocking single byte read from the receive ring buffer.
 *
 * Reception is interrupt driven precisely so a slow caller cannot lose bytes:
 * the USART has a one byte register and no FIFO, so at 115200 baud any polling
 * loop slower than 87 us drops characters.
 */
bool cads_hal_console_read(uint8_t* byte);

/** Bytes discarded because the reader fell behind the ring buffer. */
uint32_t cads_hal_console_dropped(void);

/** Hardware receive overruns. Non-zero means bytes were lost before the ISR. */
uint32_t cads_hal_console_overruns(void);

/* --- WiFi co-processor link (USART6 -> ESP32 on hardware, no-op in the
 * simulator - see docs/reference/wifi-coprocessor.md). Byte-oriented on
 * purpose: modules/wifi frames PPP over this, the same way the console frames
 * command lines over its own byte stream. */

void cads_hal_wifi_uart_init(void);
void cads_hal_wifi_uart_write(const void* data, size_t length);
bool cads_hal_wifi_uart_read(uint8_t* byte);
uint32_t cads_hal_wifi_uart_dropped(void);
uint32_t cads_hal_wifi_uart_overruns(void);

/* --- display ------------------------------------------------------------- */

void cads_hal_display_init(void);

/** Backlight duty in percent, 0..100. */
void cads_hal_display_backlight(uint8_t percent);

/**
 * Select the display bus clock.
 *
 * false = the always-safe divider, true = the faster one. The limit is the
 * shield's 74HC4094 shift register chain rather than the panel, so the fast
 * setting is only ever enabled after being qualified on real hardware.
 * See docs/SAFETY.md, "Raising the SPI clock".
 *
 * No-op in the simulator.
 */
void cads_hal_display_set_fast_clock(bool fast);

/*
 * Mask/unmask interrupts, returning the previous state. On the board this is
 * PRIMASK save/disable/restore - safe both before and after the scheduler
 * starts (unlike taskENTER_CRITICAL, whose nesting counter holds the CM4F
 * port's poison value pre-scheduler; see the SPI-mutex boot-crash lesson).
 * On a single core, masking interrupts also stops task preemption, so a
 * few-instruction window bounded by these calls is atomic between tasks.
 * The host/sim implementation is a no-op (single-threaded).
 * Keep the window to a handful of instructions.
 */
uint32_t cads_hal_irq_save(void);
void cads_hal_irq_restore(uint32_t state);

/**
 * Push a rectangle of RGB565 pixels to the panel.
 *
 * The buffer must stay valid until cads_hal_display_busy() reports false: on
 * hardware the transfer is handed to DMA and returns immediately.
 * On hardware the source buffer must live in DMA-capable SRAM, never in CCM.
 */
void cads_hal_display_blit(
    uint16_t x,
    uint16_t y,
    uint16_t width,
    uint16_t height,
    const uint16_t* pixels);

/** True while a blit is still in flight. */
bool cads_hal_display_busy(void);

/** Block until any in-flight blit has completed. */
void cads_hal_display_wait(void);

/* --- touch panel (XPT2046) ----------------------------------------------- */

typedef struct {
    uint16_t x;        /**< 0..CADS_DISPLAY_WIDTH-1, only valid when pressed */
    uint16_t y;        /**< 0..CADS_DISPLAY_HEIGHT-1                          */
    uint16_t pressure; /**< raw, larger means firmer contact                  */
    bool pressed;
} cads_touch_state_t;

void cads_hal_touch_read(cads_touch_state_t* state);

/* --- ITS adapter board I/O ----------------------------------------------- */

/** IN0..IN7 on GPIOF, active low (pulled up). Bit n = INn. */
uint8_t cads_hal_adapter_inputs(void);

/** INT0..INT5 on GPIOG, active low. Bit n = INTn. */
uint8_t cads_hal_adapter_interrupts(void);

/** OUT0..OUT15: bits 0..7 go to GPIOD, bits 8..15 to GPIOE. */
void cads_hal_adapter_outputs(uint16_t value);

/* --- on-board indicators -------------------------------------------------- */

typedef enum {
    CadsLedGreen,
    CadsLedBlue,
    CadsLedRed,
} cads_led_t;

void cads_hal_led_set(cads_led_t led, bool on);
void cads_hal_led_toggle(cads_led_t led);

/** The blue USER button on the Nucleo, active high. */
bool cads_hal_user_button(void);

/* --- raw port inspection --------------------------------------------------
 *
 * Used only by the hardware explorer, which exists because the ITS adapter's
 * wiring is not documented anywhere we have. Exposed through the HAL rather
 * than letting a tool in apps/ reach for the device headers directly - the rule
 * that everything above the HAL builds for both targets is worth more than the
 * convenience.
 */

/** Number of inspectable GPIO ports on this target. */
uint32_t cads_hal_port_count(void);

/** Single-letter name of a port, 'A'..'K' on the board. */
char cads_hal_port_name(uint32_t index);

/** Raw input register of a port. */
uint16_t cads_hal_port_read(uint32_t index);

/**
 * True for pins that must not be repurposed: SWD, the HSE input, and the RMII
 * lines. The explorer flags them so a reader is never tempted to wire a button
 * to one. See docs/SAFETY.md.
 */
bool cads_hal_pin_is_reserved(uint32_t port_index, uint32_t pin);

/* --- fault reporting ------------------------------------------------------ */

/**
 * Last-resort failure path: prints the reason on the console, lights the red
 * LED and halts. Never returns. On hardware it issues a breakpoint so an
 * attached debugger stops with the machine state intact.
 */
__attribute__((noreturn)) void cads_hal_panic(const char* reason);

/* --- watchdog and crash forensics -------------------------------------------
 *
 * The independent watchdog (IWDG) exists to turn "the firmware locked up and
 * an untethered board just sits there with the red LED on" (docs/SAFETY.md's
 * own documented failure mode for a debugger-less panic) into "the board
 * recovers on its own, and what caused the lockup is still readable after it
 * does". Two halves:
 *
 *   1. cads_hal_watchdog_init()/feed() - modules/kernel's vApplicationTickHook
 *      feeds it once per SysTick (1 kHz), NOT from any application task. That
 *      is a deliberate scope choice, not an oversight: feeding from the tick
 *      proves the interrupt subsystem is alive and reliably recovers from a
 *      true lockup (a HardFault recursion loop, interrupts globally
 *      disabled), with ZERO risk of a spurious reset during any legitimate
 *      long-running operation (a 448 ms display flush, a multi-minute
 *      explorer demo) - none of those ever stop the tick from running. It
 *      does NOT catch a cooperative task spinning forever on something that
 *      will never happen while interrupts keep flowing; that is a different,
 *      harder problem this feature does not claim to solve.
 *
 *   2. cads_hal_reset_cause() - decodes RCC->CSR (STM32) before anything
 *      clears it, so a reset that the watchdog itself caused is
 *      distinguishable from a normal power-on or a debugger-driven reset.
 *      Latched once per boot: the first call reads and clears the hardware
 *      flags, every later call in the same boot returns the same cached
 *      answer.
 *
 * modules/diag/include/cads/diag/forensic.h is the portable ring buffer that
 * actually records what a fault handler or cads_hal_panic() saw; this HAL
 * layer only supplies the two hardware primitives it and the recovery path
 * need. See docs/SAFETY.md - neither of these touches a single GPIO pin, so
 * none of the binding pin rules apply.
 */

typedef enum {
    CadsResetUnknown = 0,   /**< First boot after flashing, or cause unreadable. */
    CadsResetPowerOn,       /**< POR/BOR: the board was actually powered up. */
    CadsResetPin,           /**< NRST driven low - the physical reset button/ST-Link. */
    CadsResetSoftware,      /**< NVIC_SystemReset() / AIRCR.SYSRESETREQ. */
    CadsResetWatchdogIndependent, /**< IWDG timed out: something stopped feeding it. */
    CadsResetWatchdogWindow,      /**< WWDG timed out. Not used by this firmware today. */
    CadsResetLowPower,      /**< Illegal low-power entry, per RM0090. */
} cads_reset_cause_t;

/**
 * What caused THIS boot. Safe to call repeatedly and from any context; the
 * underlying hardware register is read and cleared exactly once regardless
 * of how many times this is called.
 */
cads_reset_cause_t cads_hal_reset_cause(void);

/**
 * Arms the independent watchdog with the given timeout and freezes it
 * whenever a debugger halts the core (STM32's DBGMCU_APB1_FZ_DBG_IWDG_STOP),
 * so attaching ST-Link/GDB to inspect a live panic never races a surprise
 * reset out from under the session - the documented "halts usefully with a
 * debugger attached" behaviour in docs/SAFETY.md stays true.
 *
 * IWDG cannot be stopped once started (RM0090 20.3.2); this is a one-way
 * door for the life of the running image. Call once, during startup, before
 * anything that could plausibly loop forever.
 */
void cads_hal_watchdog_init(uint32_t timeout_ms);

/** Kicks the watchdog. See the tick-hook note above for who calls this. */
void cads_hal_watchdog_feed(void);

/* --- hardware random number generator -------------------------------------
 *
 * STM32F429's RNG peripheral (RM0090 ch. 24) - a real analog entropy source
 * (ring oscillators XORed together), not a PRNG. Added 2026-08-28 as the
 * nonce source for modules/security's AEAD link (see that module's own
 * header): a fixed pre-shared key with an XChaCha20-Poly1305 24-byte random
 * nonce per message needs a genuine entropy source, not `rand()`.
 *
 * cads_hal_rng_bytes() implements RM0090's own documented procedure
 * end to end, not a simplified version of it:
 *   - the FIPS 140-2 continuous-RNG self-test the manual itself calls for
 *     (discard the first word after enabling; every later word must differ
 *     from the one before it, or the call fails - catches a stuck-at fault
 *     in the analog seed, which is exactly what that test exists to catch);
 *   - SECS/CECS live error checking on every word, not just once at start
 *     (a seed error can begin mid-stream);
 *   - a bounded retry count per word (matching hal_clock.c's own bounded-spin
 *     convention for hardware polling - RM0090 6.3.1/20.3.2 style), so a
 *     genuine hardware RNG fault reports failure instead of hanging the
 *     caller forever.
 * Returns false (and leaves `out` in whatever partial state it was in when
 * the failure happened) on any error - the caller MUST treat that as "no
 * nonce available" and refuse to encrypt, never fall back to a weaker
 * source silently. */
bool cads_hal_rng_bytes(uint8_t* out, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* CADS_HAL_H */
