/*
 * CaDS Zero - on-board indicators, ITS adapter I/O, and the panic path.
 *
 * Direction discipline for the adapter board (docs/SAFETY.md):
 *   PD0..PD7, PE0..PE7  outputs, driving the adapter's LED banks
 *   PF0..PF7            INPUTS, pulled up
 *   PG0..PG5            INPUTS, pulled up
 *
 * PF and PG are never configured as outputs. Whatever the adapter has wired to
 * them may be driving those lines, and two push-pull drivers fighting over one
 * net is how boards die.
 */

#include "board.h"
#include "cads/diag/forensic.h"
#include "cads_hal.h"
#include "hal_gpio.h"

void cads_hal_io_init(void) {
    /* Every GPIO port's clock, so reading any IDR returns real pin state rather
     * than zeroes. Costs a few microamps and makes the hardware explorer able
     * to see the whole device. Enabling a clock does not change any pin's
     * direction, so this is safe by construction. */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN |
                    RCC_AHB1ENR_GPIODEN | RCC_AHB1ENR_GPIOEEN | RCC_AHB1ENR_GPIOFEN |
                    RCC_AHB1ENR_GPIOGEN | RCC_AHB1ENR_GPIOHEN | RCC_AHB1ENR_GPIOIEN |
                    RCC_AHB1ENR_GPIOJEN | RCC_AHB1ENR_GPIOKEN;
    (void)RCC->AHB1ENR;

    /* Output banks: start at zero so nothing lights up before the firmware
     * decides it should. */
    cads_gpio_clock_enable(CADS_PIN_OUT_LOW_PORT);
    cads_gpio_clock_enable(CADS_PIN_OUT_HIGH_PORT);
    for(uint32_t pin = 0; pin < 8u; pin++) {
        cads_gpio_init_output(CADS_PIN_OUT_LOW_PORT, pin, false);
        cads_gpio_init_output(CADS_PIN_OUT_HIGH_PORT, pin, false);
    }

    /* Input banks. Pull-ups because the adapter's switches pull to ground. */
    for(uint32_t pin = 0; pin < 8u; pin++) {
        cads_gpio_init_input(CADS_PIN_IN_PORT, pin, CadsGpioPullUp);
    }
    for(uint32_t pin = 0; pin < 6u; pin++) {
        cads_gpio_init_input(CADS_PIN_INT_PORT, pin, CadsGpioPullUp);
    }

    cads_gpio_init_output(CADS_PIN_LED_GREEN_PORT, CADS_PIN_LED_GREEN, false);
    cads_gpio_init_output(CADS_PIN_LED_BLUE_PORT, CADS_PIN_LED_BLUE, false);
    cads_gpio_init_output(CADS_PIN_LED_RED_PORT, CADS_PIN_LED_RED, false);
    cads_gpio_init_input(CADS_PIN_USER_BTN_PORT, CADS_PIN_USER_BTN, CadsGpioPullNone);
}

uint8_t cads_hal_adapter_inputs(void) {
    /* Active low on the wire; report active high so callers read naturally. */
    return (uint8_t)(~CADS_PIN_IN_PORT->IDR) & 0xFFu;
}

uint8_t cads_hal_adapter_interrupts(void) {
    return (uint8_t)((~CADS_PIN_INT_PORT->IDR) & CADS_ADAPTER_INT_MASK);
}

void cads_hal_adapter_outputs(uint16_t value) {
    /* BSRR in one write per port: set and clear atomically, so a concurrent
     * read-modify-write elsewhere cannot lose a bit. */
    uint32_t low = value & 0xFFu;
    uint32_t high = (value >> 8) & 0xFFu;
    CADS_PIN_OUT_LOW_PORT->BSRR = low | ((~low & 0xFFu) << 16);
    CADS_PIN_OUT_HIGH_PORT->BSRR = high | ((~high & 0xFFu) << 16);
}

static GPIO_TypeDef* cads_led_port(cads_led_t led) {
    switch(led) {
    case CadsLedBlue: return CADS_PIN_LED_BLUE_PORT;
    case CadsLedRed: return CADS_PIN_LED_RED_PORT;
    case CadsLedGreen:
    default: return CADS_PIN_LED_GREEN_PORT;
    }
}

static uint32_t cads_led_pin(cads_led_t led) {
    switch(led) {
    case CadsLedBlue: return CADS_PIN_LED_BLUE;
    case CadsLedRed: return CADS_PIN_LED_RED;
    case CadsLedGreen:
    default: return CADS_PIN_LED_GREEN;
    }
}

void cads_hal_led_set(cads_led_t led, bool on) {
    cads_gpio_write(cads_led_port(led), cads_led_pin(led), on);
}

void cads_hal_led_toggle(cads_led_t led) {
    cads_gpio_toggle(cads_led_port(led), cads_led_pin(led));
}

bool cads_hal_user_button(void) {
    return cads_gpio_read(CADS_PIN_USER_BTN_PORT, CADS_PIN_USER_BTN);
}

__attribute__((noreturn)) void cads_hal_panic(const char* reason) {
    __disable_irq();

    cads_gpio_write(CADS_PIN_LED_RED_PORT, CADS_PIN_LED_RED, true);

    /* The console is polled and interrupt-free, so it still works here. */
    static const char prefix[] = "\r\n*** CaDS PANIC: ";
    cads_hal_console_write(prefix, sizeof(prefix) - 1u);
    if(reason) {
        size_t length = 0u;
        while(reason[length] && length < 200u) length++;
        cads_hal_console_write(reason, length);
    }
    cads_hal_console_write(" ***\r\n", 6u);

    /* No exception frame here - this is a plain C call, not a hardware
     * fault entry - so the ring records the reason string and whatever
     * CFSR/HFSR happen to hold (typically 0: most panics, like a stack
     * overflow or an LWIP_PLATFORM_ASSERT, are software checks, not CPU
     * faults) rather than fabricating register state that was never
     * pushed. See fault_handlers.c's own use of this same ring for the
     * exception-entry case, which does have a real frame. */
    uint32_t cfsr = SCB->CFSR;
    bool mmfar_valid = (cfsr & SCB_CFSR_MMARVALID_Msk) != 0u;
    bool bfar_valid = (cfsr & SCB_CFSR_BFARVALID_Msk) != 0u;
    cads_forensic_record(
        reason, NULL, cfsr, SCB->HFSR, mmfar_valid, SCB->MMFAR, bfar_valid, SCB->BFAR);

    /* Stop with everything intact so the attached ST-Link can inspect it. */
    __asm volatile("bkpt #0" ::: "memory");
    for(;;) {
    }
}
