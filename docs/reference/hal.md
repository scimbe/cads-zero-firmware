# HAL interface

The boundary between portable code and hardware. Header: `core/cads_hal.h`.

Everything above this line compiles unchanged for the board and for the host
simulator. Two implementations exist: `targets/itsboard/` against STM32F429
registers, and `targets/sim/` against SDL2.

To bring up different hardware, see
[Port to different hardware](../how-to/port-to-new-board.md).

## Board identity

```c
const cads_board_info_t* board = cads_hal_board_info();
```

Ask this rather than assuming. The fields that matter most:

| Field | Why it exists |
|---|---|
| `display_width` / `display_height` | the panel's size, which is not necessarily the framebuffer's |
| `display_readable` | **false** here: the bus has no return path, so nothing can read video memory back |
| `button_count` | the soft-key strip sizes itself from this |
| `display_pixels_per_second` | measured, 342 000 — a GUI deciding whether an animation is affordable should look it up |
| `has_network` / `has_touch` / `has_storage` | capability tests instead of board tests |

`CADS_DISPLAY_WIDTH` / `HEIGHT` are the **compile-time maxima** the static
framebuffer is sized from. Layout code should use the descriptor.

## Lifecycle

```c
void cads_hal_early_init(void);   /* clocks; runs before the C runtime is usable */
void cads_hal_init(void);         /* everything else */
```

## Time

```c
uint32_t cads_hal_ticks_ms(void);
uint64_t cads_hal_ticks_us(void);
void cads_hal_delay_us(uint32_t us);
void cads_hal_delay_ms(uint32_t ms);
```

Derived from the DWT cycle counter, not from a tick interrupt, so time is
correct inside critical sections and ISRs. SysTick belongs to the scheduler.

## Display

```c
void cads_hal_display_blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                           const uint16_t* pixels);
bool cads_hal_display_busy(void);
void cads_hal_display_wait(void);
void cads_hal_display_backlight(uint8_t percent);
void cads_hal_display_set_fast_clock(bool fast);
```

Two contracts that are easy to violate:

- `pixels` must stay valid until `cads_hal_display_busy()` returns false — the
  transfer is handed to DMA and the call returns immediately.
- On hardware the buffer must live in **DMA-capable SRAM**. CCM is invisible to
  every DMA controller on this part, and a transfer from there produces nothing
  at all, with no error.

## Touch, buttons, indicators

```c
void cads_hal_touch_read(cads_touch_state_t* state);
uint8_t cads_hal_adapter_inputs(void);       /* bit n = Sn pressed */
uint8_t cads_hal_adapter_interrupts(void);
void cads_hal_adapter_outputs(uint16_t value);
void cads_hal_led_set(cads_led_t led, bool on);
bool cads_hal_user_button(void);
```

`cads_hal_adapter_inputs()` already inverts the board's active-low wiring, so a
set bit means pressed regardless of the hardware's polarity. That inversion
lives here and nowhere else.

## Console

```c
void cads_hal_console_write(const void* data, size_t length);
bool cads_hal_console_read(uint8_t* byte);
uint32_t cads_hal_console_dropped(void);
uint32_t cads_hal_console_overruns(void);
```

Receive is interrupt driven with a ring buffer, and that is a requirement
rather than an optimisation: the STM32F4 USART has a one-byte receive register
with no FIFO, so at 115200 baud any polling loop slower than 87 µs drops
characters. The two counters exist so such loss can never again be silent —
it once presented as a display fault.

## Panic

```c
__attribute__((noreturn)) void cads_hal_panic(const char* reason);
```

Prints, lights the red LED, and halts with the machine intact for a debugger.
With no debugger attached the breakpoint escalates to a lock-up, which is the
intended safe failure mode rather than a reset loop that hides the cause.
