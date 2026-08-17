/*
 * CaDS Zero - raw GPIO port inspection for the hardware explorer.
 *
 * Read-only by construction: nothing here writes MODER, so it can never create
 * pin contention no matter what the explorer is asked to look at.
 */

#include "board.h"
#include "cads_hal.h"

static GPIO_TypeDef* const cads_all_ports[] = {
    GPIOA, GPIOB, GPIOC, GPIOD, GPIOE, GPIOF, GPIOG, GPIOH, GPIOI, GPIOJ, GPIOK};
static const char cads_port_names[] = "ABCDEFGHIJK";

#define CADS_PORT_COUNT (sizeof(cads_all_ports) / sizeof(cads_all_ports[0]))

uint32_t cads_hal_port_count(void) {
    return CADS_PORT_COUNT;
}

char cads_hal_port_name(uint32_t index) {
    return index < CADS_PORT_COUNT ? cads_port_names[index] : '?';
}

uint16_t cads_hal_port_read(uint32_t index) {
    return index < CADS_PORT_COUNT ? (uint16_t)cads_all_ports[index]->IDR : 0u;
}

bool cads_hal_pin_is_reserved(uint32_t port_index, uint32_t pin) {
    switch(cads_hal_port_name(port_index)) {
    case 'A':
        /* SWDIO, SWCLK, and three RMII lines. */
        return pin == 13u || pin == 14u || pin == 1u || pin == 2u || pin == 7u;
    case 'B':
        return pin == 3u /* SWO */ || pin == 13u /* RMII TXD1 */;
    case 'C':
        return pin == 1u || pin == 4u || pin == 5u; /* RMII */
    case 'G':
        return pin == 2u || pin == 11u || pin == 13u; /* RMII */
    case 'H':
        return pin == 0u || pin == 1u; /* HSE input from the ST-Link MCO */
    default:
        return false;
    }
}
