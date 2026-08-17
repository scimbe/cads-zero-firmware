/*
 * CaDS Zero - console on USART3, which the Nucleo routes to the ST-Link
 * virtual COM port (PD8 = TX, PD9 = RX, AF7).
 *
 * This is the channel the automated board tests speak over, so it stays
 * deliberately dumb: polled, no DMA, no ring buffer on the TX side. A test
 * harness that cannot trust its own transport is worthless.
 */

#include "board.h"
#include "cads_hal.h"
#include "hal_gpio.h"

void cads_hal_console_init(uint32_t baud) {
    cads_gpio_init_alternate(CADS_PIN_UART_TX_PORT, CADS_PIN_UART_TX, CADS_UART_AF, CadsGpioPullUp);
    cads_gpio_init_alternate(CADS_PIN_UART_RX_PORT, CADS_PIN_UART_RX, CADS_UART_AF, CadsGpioPullUp);

    RCC->APB1ENR |= RCC_APB1ENR_USART3EN;
    (void)RCC->APB1ENR;

    CADS_CONSOLE_UART->CR1 = 0u; /* disable while reconfiguring */

    /* Oversampling by 16: BRR is simply PCLK/baud in 1/16th steps, which is
     * exactly the fixed point layout of the register. */
    CADS_CONSOLE_UART->BRR = (CADS_PCLK1_HZ + (baud / 2u)) / baud;

    CADS_CONSOLE_UART->CR2 = 0u; /* 1 stop bit */
    CADS_CONSOLE_UART->CR3 = 0u; /* no flow control */
    CADS_CONSOLE_UART->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

void cads_hal_console_write(const void* data, size_t length) {
    const uint8_t* bytes = (const uint8_t*)data;
    for(size_t i = 0; i < length; i++) {
        while(!(CADS_CONSOLE_UART->SR & USART_SR_TXE)) {
        }
        CADS_CONSOLE_UART->DR = bytes[i];
    }
    while(!(CADS_CONSOLE_UART->SR & USART_SR_TC)) {
    }
}

bool cads_hal_console_read(uint8_t* byte) {
    if(!(CADS_CONSOLE_UART->SR & USART_SR_RXNE)) {
        return false;
    }
    *byte = (uint8_t)(CADS_CONSOLE_UART->DR & 0xFFu);
    return true;
}
