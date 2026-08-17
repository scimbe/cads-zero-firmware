/*
 * CaDS Zero - direct GPIO register helpers for STM32F4.
 *
 * Deliberately register level rather than ST HAL: every line of it is visible,
 * it inlines to two or three instructions, and it keeps the firmware free of a
 * vendor HAL whose init structs hide which bits actually changed.
 */

#ifndef CADS_HAL_GPIO_H
#define CADS_HAL_GPIO_H

#include <stdbool.h>
#include <stdint.h>

#include "stm32f4xx.h"

typedef enum {
    CadsGpioModeInput = 0u,
    CadsGpioModeOutput = 1u,
    CadsGpioModeAlternate = 2u,
    CadsGpioModeAnalog = 3u,
} cads_gpio_mode_t;

typedef enum {
    CadsGpioPullNone = 0u,
    CadsGpioPullUp = 1u,
    CadsGpioPullDown = 2u,
} cads_gpio_pull_t;

typedef enum {
    CadsGpioSpeedLow = 0u,
    CadsGpioSpeedMedium = 1u,
    CadsGpioSpeedFast = 2u,
    CadsGpioSpeedHigh = 3u,
} cads_gpio_speed_t;

/** Enable the AHB1 clock of a GPIO port. Safe to call repeatedly. */
static inline void cads_gpio_clock_enable(const GPIO_TypeDef* port) {
    uint32_t bit = ((uint32_t)port - AHB1PERIPH_BASE) / 0x400u;
    RCC->AHB1ENR |= (1u << bit);
    (void)RCC->AHB1ENR; /* guarantee the write landed before the port is used */
}

static inline void cads_gpio_set_mode(
    GPIO_TypeDef* port,
    uint32_t pin,
    cads_gpio_mode_t mode,
    cads_gpio_pull_t pull,
    cads_gpio_speed_t speed) {
    port->MODER = (port->MODER & ~(3u << (pin * 2u))) | ((uint32_t)mode << (pin * 2u));
    port->PUPDR = (port->PUPDR & ~(3u << (pin * 2u))) | ((uint32_t)pull << (pin * 2u));
    port->OSPEEDR = (port->OSPEEDR & ~(3u << (pin * 2u))) | ((uint32_t)speed << (pin * 2u));
}

static inline void cads_gpio_set_alternate(GPIO_TypeDef* port, uint32_t pin, uint32_t af) {
    volatile uint32_t* reg = &port->AFR[pin >> 3u];
    uint32_t shift = (pin & 7u) * 4u;
    *reg = (*reg & ~(0xFu << shift)) | (af << shift);
}

/** Configure a pin as push-pull output and drive it to `initial`. */
static inline void cads_gpio_init_output(GPIO_TypeDef* port, uint32_t pin, bool initial) {
    cads_gpio_clock_enable(port);
    port->BSRR = initial ? (1u << pin) : (1u << (pin + 16u));
    port->OTYPER &= ~(1u << pin);
    cads_gpio_set_mode(port, pin, CadsGpioModeOutput, CadsGpioPullNone, CadsGpioSpeedHigh);
}

static inline void cads_gpio_init_input(GPIO_TypeDef* port, uint32_t pin, cads_gpio_pull_t pull) {
    cads_gpio_clock_enable(port);
    cads_gpio_set_mode(port, pin, CadsGpioModeInput, pull, CadsGpioSpeedFast);
}

static inline void
    cads_gpio_init_alternate(GPIO_TypeDef* port, uint32_t pin, uint32_t af, cads_gpio_pull_t pull) {
    cads_gpio_clock_enable(port);
    cads_gpio_set_alternate(port, pin, af);
    port->OTYPER &= ~(1u << pin);
    cads_gpio_set_mode(port, pin, CadsGpioModeAlternate, pull, CadsGpioSpeedHigh);
}

static inline void cads_gpio_write(GPIO_TypeDef* port, uint32_t pin, bool value) {
    port->BSRR = value ? (1u << pin) : (1u << (pin + 16u));
}

static inline bool cads_gpio_read(const GPIO_TypeDef* port, uint32_t pin) {
    return (port->IDR & (1u << pin)) != 0u;
}

static inline void cads_gpio_toggle(GPIO_TypeDef* port, uint32_t pin) {
    port->ODR ^= (1u << pin);
}

#endif /* CADS_HAL_GPIO_H */
